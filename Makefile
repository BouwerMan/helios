export DEFAULT_HOST=i686-elf
export HOST?=$(DEFAULT_HOST)
export HOSTARCH=i386

export PREFIX=$(HOME)/opt/cross/bin
export TARGET=i386-elf
#export PATH=$(PREFIX)/bin:$(PATH)

export OSNAME=HeliOS

# export MAKE=$(MAKE:-make)
# export HOST=$(HOST:-$(./default-host.sh))

export AR:=$(PREFIX)/$(HOST)-ar
export AS:=$(PREFIX)/$(HOST)-as
export CC:=$(PREFIX)/$(HOST)-gcc

export PREFIX=/usr
export EXEC_PREFIX=$(PREFIX)
export BOOTDIR=/boot
export LIBDIR=$(EXEC_PREFIX)/lib
export INCLUDEDIR=$(PREFIX)/include

export CWARN=-Wall -Wextra -pedantic -std=gnu23
export CFLAGS=-O2 -g
export CPPFLAGS=

# Configure the cross-compiler to use the desired system root.
export SYSROOT="$(shell pwd)/sysroot"
export CC+=--sysroot=$(SYSROOT) -isystem=$(INCLUDEDIR)

.PHONY: all libc helios clean qemu headers iso bochs

all: headers libc helios

helios:
	DESTDIR=$(SYSROOT) $(MAKE) -C ./HeliOS install

libc:
	DESTDIR=$(SYSROOT) $(MAKE)  -C ./libc install

headers:
	mkdir -p $(SYSROOT)
	DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install-headers
	DESTDIR=$(SYSROOT) $(MAKE) -C ./HeliOS install-headers

iso: all
	mkdir -p isodir
	mkdir -p isodir/boot
	mkdir -p isodir/boot/grub

	cp sysroot/boot/$(OSNAME).kernel isodir/boot/$(OSNAME).kernel
	cp grub.cfg isodir/boot/grub
	grub-mkrescue -o $(OSNAME).iso isodir

qemu: iso
	qemu-system-$(HOSTARCH) -cdrom $(OSNAME).iso -m 4096M -hdd ./fat.img -boot d -s

bochs: iso
	bochs -f bochs

clean:
	rm -rvf sysroot
	rm -rvf isodir
	rm -rvf *.iso
	$(MAKE) -C ./libc clean
	$(MAKE) -C ./HeliOS clean
