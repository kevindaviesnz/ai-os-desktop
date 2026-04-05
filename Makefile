CC = aarch64-elf-gcc
LD = aarch64-elf-ld
OBJCOPY = aarch64-elf-objcopy

CFLAGS = -ffreestanding -mcpu=cortex-a53 -O2 -Wall -Wextra -Iinclude
LDFLAGS = -T linker.ld -nostdlib

KERNEL_SRC = kernel/boot.S kernel/vectors.S kernel/main.c kernel/mmu.c kernel/uart.c kernel/gic.c kernel/loader.c kernel/syscall.c kernel/virtio.c
KERNEL_OBJ = $(KERNEL_SRC:.c=.o)
KERNEL_OBJ := $(KERNEL_OBJ:.S=.o)

all: build/os_desktop.elf 

build/os_desktop.elf: $(KERNEL_OBJ)
	mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	@echo "[*] Launching ai-os-desktop in QEMU..."
	qemu-system-aarch64 \
		-M virt,gic-version=2 \
		-cpu cortex-a53 \
		-m 1024M \
		-display cocoa \
		-device virtio-gpu-device \
		-device virtio-mouse-device \
		-device virtio-keyboard-device \
		-serial tcp:127.0.0.1:4444,server,wait \
		-kernel build/os_desktop.elf 

clean:
	rm -rf build kernel/*.o