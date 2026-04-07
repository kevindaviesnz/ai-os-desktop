CC = aarch64-elf-gcc
LD = aarch64-elf-ld
CFLAGS = -ffreestanding -mcpu=cortex-a53 -mgeneral-regs-only -O2 -Wall -Wextra -Iinclude

OBJS = kernel/boot.o kernel/vectors.o kernel/main.o kernel/mmu.o \
       kernel/uart.o kernel/gic.o kernel/loader.o kernel/syscall.o \
       kernel/virtio.o modules/shell_gui/main.o

all: build/os_desktop.elf

build/os_desktop.elf: $(OBJS)
	mkdir -p build
	$(LD) -T linker.ld -nostdlib -o $@ $(OBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build kernel/*.o modules/shell_gui/*.o

run: build/os_desktop.elf
	qemu-system-aarch64 \
		-M virt,gic-version=2 \
		-cpu cortex-a53 \
		-m 1024M \
		-display cocoa \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-serial stdio \
		-kernel build/os_desktop.elf