export DEFAULT_HOST=x86_64-elf
export HOST?=$(DEFAULT_HOST)
export HOSTARCH=x86_64

export PREFIX=$(HOME)/opt/cross/bin
export TARGET=x86_64-elf
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

export CWARN=-Wall -Wextra -pedantic
export CFLAGS=-O2 -g -pipe -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse3 -D__KDEBUG__ -std=gnu23
export CPPFLAGS=
export GDEFINES=-D__KDEBUG__ -DLOG_LEVEL=0 -DENABLE_SERIAL_LOGGING

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
	mkdir -p isodir/boot/limine
	mkdir -p isodir/EFI/BOOT

	cp sysroot/boot/$(OSNAME).kernel isodir/boot/$(OSNAME).kernel
	cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
		  limine/limine-uefi-cd.bin isodir/boot/limine/
	cp -v limine/BOOTX64.EFI isodir/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI isodir/EFI/BOOT/
	
	# Create the bootable ISO.
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			isodir -o $(OSNAME).iso

	# Install Limine stage 1 and 2 for legacy BIOS boot.
	./limine/limine bios-install $(OSNAME).iso

qemu: iso
	qemu-system-$(HOSTARCH) -cdrom $(OSNAME).iso \
		-m 4096M \
		-hdd ./fat.img -boot d -s \
		-serial stdio \

bochs: iso
	bochs -f bochs

clean:
	rm -rvf sysroot
	rm -rvf isodir
	rm -rvf *.iso
	$(MAKE) -C ./libc clean
	$(MAKE) -C ./HeliOS clean
