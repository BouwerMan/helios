PREFIX="$(HOME)/opt/cross"
TARGET=i686-elf
export PATH := "$(PREFIX)/bin:$(PATH)"
export PATH := "$(HOME)/opt/cross/bin:$(PATH)"

CC=$(PREFIX)/bin/$(TARGET)-gcc

CFLAGS := -std=c17 -ffreestanding -O2 -Wall -Wextra

all:
	$(CC) $(CFLAGS) src/kernel.c -o build/kernel.o
