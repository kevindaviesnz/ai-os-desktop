CC = aarch64-elf-gcc
LD = aarch64-elf-ld
CFLAGS = -ffreestanding -mcpu=cortex-a53 -mgeneral-regs-only -O2 -Wall -Wextra -Iinclude

# Notice we removed kernel/autarky.o from this list!
OBJS = kernel/boot.o kernel/vectors.o kernel/main.o kernel/mmu.o \
       kernel/uart.o kernel/gic.o kernel/loader.o kernel/syscall.o \
       kernel/virtio.o kernel/virtio_net.o kernel/watcher.o \
       modules/shell_gui/main.o modules/fs/fat32.o

# The path to the new Rust engine
RUST_LIB = autarky_vm/target/aarch64-unknown-none/release/libautarky_core.a

all: build/os_desktop.elf

# Tell Make to compile the Rust code
$(RUST_LIB):
	cd autarky_vm && cargo build --target aarch64-unknown-none --release --lib

# Link the C objects and the Rust library together
build/os_desktop.elf: $(OBJS) $(RUST_LIB)
	mkdir -p build
	$(LD) -T linker.ld -nostdlib -o $@ $(OBJS) $(RUST_LIB)

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build kernel/*.o modules/shell_gui/*.o modules/fs/*.o
	cd autarky_vm && cargo clean

run: build/os_desktop.elf
	qemu-system-aarch64 \
		-M virt,gic-version=2 \
		-cpu cortex-a53 \
		-m 1024M \
		-display cocoa \
		-drive file=disk.img,if=none,format=raw,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-device virtio-gpu-device \
		-device virtio-keyboard-device \
		-serial stdio \
		-kernel build/os_desktop.elf \
		-netdev socket,id=net0,udp=127.0.0.1:8080,localaddr=127.0.0.1:8081 \
		-device virtio-net-device,netdev=net0