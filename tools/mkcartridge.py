import sys
import struct
import subprocess
import shutil
import os

MAGIC_ATKM = 0x4D4B5441

def get_section_size(elf_path, section_name):
    objdump = shutil.which('aarch64-elf-objdump') or \
              shutil.which('aarch64-none-elf-objdump')

    if not objdump:
        print(f"FATAL: No AArch64 objdump found in PATH")
        sys.exit(1)

    try:
        result = subprocess.run(
            [ objdump, '-h', elf_path ],
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print(f"FATAL: objdump returned {result.returncode} on {elf_path}")
            sys.exit(1)

        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[ 1 ] == section_name:
                return int(parts[ 2 ], 16)

    except Exception as e:
        print(f"FATAL: Exception running objdump: {e}")
        sys.exit(1)

    return 0  

if len(sys.argv) < 6:
    print("Usage: mkcartridge.py <in.bin> <in.elf> <out.atkm> <module_id> <stack_size>")
    sys.exit(1)

bin_path   = sys.argv[ 1 ]
elf_path   = sys.argv[ 2 ]
out_path   = sys.argv[ 3 ]
module_id  = int(sys.argv[ 4 ])
stack_size = int(sys.argv[ 5 ])

with open(bin_path, 'rb') as f:
    code_data = f.read()

code_size       = len(code_data)
bss_size        = get_section_size(elf_path, '.bss')
text_size       = get_section_size(elf_path, '.text')
rodata_size     = get_section_size(elf_path, '.rodata')
executable_size = text_size + rodata_size

if executable_size == 0:
    print(f"FATAL: executable_size is 0 for {elf_path} — .text and .rodata both absent")
    sys.exit(1)

signature = b'\x00' * 64
header = struct.pack('<IIIIII64s',
    MAGIC_ATKM, module_id, code_size,
    executable_size, bss_size, stack_size, signature)

with open(out_path, 'wb') as f:
    f.write(header)
    f.write(code_data)

print(f"Packed {out_path}: ID={module_id}, Code={code_size}b, "
      f"Text={executable_size}b, BSS={bss_size}b, Stack={stack_size}b")