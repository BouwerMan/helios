PREFIX="$(HOME)/opt/cross"
TARGET=i686-elf
#PATH="$(PREFIX)/bin:($PATH)"
#PATH="$(HOME)/opt/cross/bin:$(PATH)"

CC=$(PREFIX)/bin/$(TARGET)-gcc

CFLAGS := -std=c17 -ffreestanding -O2 -Wall -Wextra

all:
	$(CC) $(CFLAGS) kernel.c -o kernel.o
