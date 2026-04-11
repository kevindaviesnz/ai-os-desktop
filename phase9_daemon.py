import subprocess
import sys
import time

print("[DAEMON] Booting Autonomous Agent Bridge...")

# We add stdin=subprocess.PIPE so Python can inject keystrokes
proc = subprocess.Popen(
    ["make", "run"], 
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE, 
    text=True, 
    bufsize=1
)

log_buffer = ""
is_recording = False

def inject_command(cmd):
    """Simulates an AI typing a command into the OS."""
    print(f"\n[AGENT ACTION] Typing: '{cmd}'")
    # Send characters one by one with a slight delay so it looks like a ghost is typing
    for char in cmd + "\n":
        proc.stdin.write(char)
        proc.stdin.flush()
        time.sleep(0.05)

def mock_agent_decide(memory_dump):
    """A local parser that decides what action to take based on OS memory."""
    if "Ctx: NEW.TXT" in memory_dump and "Type: 0x0000000000000002" in memory_dump:
        print("\n[AGENT THOUGHT] The user just read NEW.TXT. I should clean up the terminal and show them the directory again.")
        return "clear\ns.list"
    
    print("\n[AGENT THOUGHT] I see the memory dump, but no action is required right now.")
    return None

try:
    for line in proc.stdout:
        print(line, end="") 
        
        if "=== WATCHER MEMORY DUMP ===" in line:
            is_recording = True
            log_buffer = ""
            continue
            
        if is_recording:
            if "===========================" in line:
                is_recording = False
                
                # Ask the Agent to decide on a command based on the dump
                action_cmd = mock_agent_decide(log_buffer)
                
                # If the Agent decided to do something, inject it into the OS!
                if action_cmd:
                    inject_command(action_cmd)
                
                log_buffer = ""
            else:
                log_buffer += line

except KeyboardInterrupt:
    print("\n[DAEMON] Intercepted Ctrl+C. Terminating QEMU...")
    proc.terminate()
    proc.wait()
    print("[DAEMON] Offline.")