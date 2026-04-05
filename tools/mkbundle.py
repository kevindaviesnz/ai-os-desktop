import sys
import struct
import os

MAGIC_ATKB = 0x424B5441  # "ATKB"
MAGIC_ATKM = 0x4D4B5441  # "ATKM"

# New cartridge header format — must match cartridge_header_t in os_loader.h
# uint32 magic, uint32 module_id, uint32 code_size, uint32 text_size,
# uint32 bss_size, uint32 stack_size, uint8 signature
CARTRIDGE_HEADER_FORMAT = '<IIIIII64s'
CARTRIDGE_HEADER_SIZE = struct.calcsize(CARTRIDGE_HEADER_FORMAT)

if len(sys.argv) < 3:
    print("Usage: mkbundle.py <out.atkb> <cartridge1.atkm> [cartridge2.atkm ...]")
    sys.exit(1)

out_path = sys.argv[ 1 ]
cartridge_paths = sys.argv[ 2 : ]

valid_cartridges = []
for path in cartridge_paths:
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < CARTRIDGE_HEADER_SIZE:
        print(f"Warning: Skipping {path} (too small)")
        continue
    magic = struct.unpack_from('<I', data, 0)[ 0 ]
    if magic != MAGIC_ATKM:
        print(f"Warning: Skipping {path} (Invalid Magic: 0x{magic:08x})")
        continue
    valid_cartridges.append((path, data))
    print(f"  Accepted: {path}")

# Bundle header: magic + count + index (each: module_id, offset, size)
INDEX_ENTRY_FORMAT = '<III'
INDEX_ENTRY_SIZE = struct.calcsize(INDEX_ENTRY_FORMAT)
BUNDLE_HEADER_SIZE = 8 + (INDEX_ENTRY_SIZE * 8)  # magic + count + 8 entries

# Calculate offsets
current_offset = BUNDLE_HEADER_SIZE
index_entries = []
for path, data in valid_cartridges:
    magic, module_id, code_size, text_size, bss_size, stack_size, sig = \
        struct.unpack_from(CARTRIDGE_HEADER_FORMAT, data, 0)
    index_entries.append((module_id, current_offset, len(data)))
    current_offset += len(data)

# Pad index to 8 entries
while len(index_entries) < 8:
    index_entries.append((0, 0, 0))

with open(out_path, 'wb') as f:
    f.write(struct.pack('<II', MAGIC_ATKB, len(valid_cartridges)))
    for module_id, offset, size in index_entries:
        f.write(struct.pack(INDEX_ENTRY_FORMAT, module_id, offset, size))
    for path, data in valid_cartridges:
        f.write(data)

print(f"Secure Bundle created: {out_path} ({len(valid_cartridges)} valid modules)")