import socket
import time
import urllib.request
import json

QEMU_HOST = '127.0.0.1'
QEMU_PORT = 4444
OLLAMA_MODEL = 'llama3:latest' 

def ask_ai_to_write_contract():
    print(f"[AI] Asking {OLLAMA_MODEL} to write a new Autarky contract...")
    
    system_prompt = "You are a compiler for the Autarky smart contract language. Write a 1-line script that adds 800 and 200 using prefix notation. Output NOTHING but the code. No explanation, no markdown. Example: add 800 200"    
    url = "http://localhost:11434/api/generate"
    payload = {
        "model": OLLAMA_MODEL,
        "prompt": system_prompt,
        "stream": False
    }
    
    VALID_OPS = {"add", "sub", "mul", "div", "eq", "gt", "lt", "ge", "le", "pair"}
    
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
    
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode())
            raw_code = result['response'].strip()
            
            tokens = raw_code.split()
            
            if len(tokens) >= 3 and tokens [ 0 ] .lower() in VALID_OPS:
                clean_code = f"{tokens [ 0 ] .lower()} {tokens [ 1 ]} {tokens [ 2 ]}@"
            else:
                clean_code = "add 800 200@" 
                
            print(f"[AI] Raw Output: {raw_code}")
            print(f"[AI] Sanitized Contract: {clean_code}")
            return clean_code
            
    except Exception as e:
        print(f"[AI ERROR] Make sure Ollama is running! ({e})")
        return "add 800 200@" 

def main():
    print("[DIRECTOR] Booting Neural Bridge...")
    
    ai_code = ask_ai_to_write_contract()
    
    # We are using a static, short filename to bypass the FAT32 directory limit!
    file_name = "AI.ATK"
    
    # --- macOS SOCKET FIX ---
    # The socket must be instantiated inside the loop so we don't reuse a burned BSD socket
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((QEMU_HOST, QEMU_PORT))
            break
        except ConnectionRefusedError:
            s.close() # Clean up the burned socket
            time.sleep(0.5)

    print("[DIRECTOR] Connected to OS UART.")
    print("[DIRECTOR] Injecting AI payload directly into kernel...\n")
    
    time.sleep(1.5)
    
    write_cmd = f"s.write {file_name} {ai_code}\n"
    print(f"[DIRECTOR] Typing payload at hardware speed: {write_cmd.strip()}")
    
    for char in write_cmd:
        s.sendall(char.encode('utf-8'))
        time.sleep(0.02) 
        
    time.sleep(1.0) 

    run_cmd = f"atk.run {file_name}\n"
    for char in run_cmd:
        s.sendall(char.encode('utf-8'))
        time.sleep(0.02)

    s.settimeout(2.0) 
    try:
        while True:
            data = s.recv(1024).decode('utf-8', errors='ignore')
            if not data: break
            print(data, end='', flush=True)
    except socket.timeout:
        pass

    print("\n\n[DIRECTOR] Autonomous execution complete. Terminating link.")
    s.close()

if __name__ == "__main__":
    main()