import subprocess
import sys
import time
import urllib.request
import json
import threading

print("[DAEMON] Booting Sovereign Autonomous Agent (Threaded I/O & Defensive Parsing)...")

proc = subprocess.Popen(
    ["make", "run"], 
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE, 
    text=True, 
    bufsize=1
)

def inject_command(cmd):
    """Simulates the AI typing a command into the OS."""
    print(f"\n[AGENT ACTION] Typing: '{cmd}'")
    for char in cmd + "\n":
        proc.stdin.write(char)
        proc.stdin.flush()
        time.sleep(0.05) 

def query_local_llm(memory_dump):
    """Queries the local Ollama instance for the next OS command."""
    prompt = f"""
    You are an autonomous AI Engineer wired into a bare-metal OS. 
    Your mission is to bootstrap the 'atk-exchange' financial matching engine.
    
    AVAILABLE COMMANDS:
    - s.list (Lists files)
    - s.read <filename> (Reads a file)
    - s.write <filename> <data> (Writes data to a file)
    - atk.run <filename> (Executes Autarky bytecode)
    - clear (Clears screen)
    
    RECENT OS MEMORY DUMP:
    {memory_dump}
    
    RULES:
    1. If the memory shows a ROOT_DIR list, and 'ENGINE.ATK' is NOT in the directory, you MUST write the bootstrap code. 
       Output exactly: s.write ENGINE.ATK PUSH 100 BUY PUSH 150 SELL MATCH HALT
    2. If you see that 'ENGINE.ATK' was just written or read, you MUST execute it.
       Output exactly: atk.run ENGINE.ATK
    3. YOU MUST OUTPUT ONLY THE EXACT TERMINAL COMMAND.
    4. NO MARKDOWN, NO QUOTES, NO EXPLANATIONS.
    """
    
    url = "http://localhost:11434/api/generate"
    data = json.dumps({
        "model": "llama3",
        "prompt": prompt,
        "stream": False,
        "options": {
            "temperature": 0.1 
        }
    }).encode('utf-8')
    
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
    
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode('utf-8'))
            raw_response = result.get("response", "").strip()
            
            # Defensive Parsing: Isolate the actual shell command
            valid_starts = ("s.write", "s.read", "atk.run", "clear", "s.list")
            clean_cmd = None
            
            for line in raw_response.split('\n'):
                line = line.strip().replace("```", "").replace("bash", "")
                if line.startswith(valid_starts):
                    clean_cmd = line
                    # Prioritize action commands if it hallucinates multiple
                    if line.startswith("s.write") or line.startswith("atk.run"):
                        break
                        
            if clean_cmd:
                print(f"\n[LLM DECISION] Sanitized to: {clean_cmd}")
                return clean_cmd
            else:
                print(f"\n[DAEMON] LLM generated invalid output: {raw_response}")
                return None
                
    except Exception as e:
        print(f"\n[DAEMON] Local LLM offline or error: {e}")
        return None

def on_dump(memory_dump):
    """Callback fired when the reader thread finishes capturing a memory dump."""
    action_cmd = query_local_llm(memory_dump)
    if action_cmd and action_cmd != "NONE":
        inject_command(action_cmd)

def reader_thread(proc, on_dump):
    """Background thread to continuously consume QEMU stdout to prevent pipe deadlocks."""
    log_buffer = ""
    is_recording = False
    
    try:
        for line in proc.stdout:
            print(line, end="", flush=True) 
            
            if "=== WATCHER MEMORY DUMP ===" in line:
                is_recording = True
                log_buffer = ""
            elif is_recording:
                if "===========================" in line:
                    is_recording = False
                    on_dump(log_buffer)
                    log_buffer = ""
                else:
                    log_buffer += line
    except Exception as e:
        print(f"\n[DAEMON] Reader thread exception: {e}")

# Spin up the reader thread
t = threading.Thread(target=reader_thread, args=(proc, on_dump), daemon=True)
t.start()

try:
    proc.wait()
except KeyboardInterrupt:
    print("\n[DAEMON] Intercepted Ctrl+C. Terminating QEMU...")
    proc.terminate()
    proc.wait()
    print("[DAEMON] Offline.")