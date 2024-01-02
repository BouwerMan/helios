set -x #echo on

RED='\033[0;31m'   #'0;31' is Red's ANSI color code
GREEN='\033[0;32m' #'0;32' is Green's ANSI color code
NOCOLOR='\033[0m'

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

mkdir build

# Assembling boot
i686-elf-as boot/boot.s -o build/boot.o
# Building
i686-elf-gcc -c src/kernel.c -o build/kernel.o -std=c17 -ffreestanding -O2 -Wall -Wextra
# Linking
i686-elf-gcc -T linker.ld -o build/myos.bin -ffreestanding -O2 -nostdlib build/boot.o build/kernel.o -lgcc

if grub-file --is-x86-multiboot build/myos.bin; then
	echo -e "${GREEN}multiboot confirmed${NOCOLOR}"
else
	echo -e "${RED}the file is not multiboot${NOCOLOR}"
	exit 1
fi
