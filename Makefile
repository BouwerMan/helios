CC = ~/opt/cross/bin/i686-elf-gcc
CCFLAGS = -Wall -Wextra -std=gnu99 -O2 -ffreestanding

AS = ~/opt/cross/bin/i686-elf-as
ASFLAGS = 

LD = ~/opt/cross/bin/i686-elf-ld
LDFLAGS = -m elf_i386 -T linker.ld

BOOTFILE = bin/boot.o
KERNELFILE = bin/kernel.o
OBJ = $(BOOTFILE) $(KERNELFILE)
OSFILE = bin/osdev.bin

VM = qemu-system-x86_64
VMFLAGS = -kernel $(OSFILE)

all: build run

build:
	mkdir -p bin
	$(AS) $(ASFLAGS) boot/boot.s -o $(BOOTFILE)
	$(CC) $(CCFLAGS) -c src/kernel.c -o $(KERNELFILE)
	$(LD) $(LDFLAGS) -o $(OSFILE) $(OBJ)

run:
	$(VM) $(VMFLAGS)

clean:
	rm -rf bin
