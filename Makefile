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

export CWARN=-Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
	-Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
        -Wredundant-decls -Wnested-externs -Wno-long-long \
        -Wconversion -Wstrict-prototypes \
	# -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wno-error=suggest-attribute=pure -Wno-error=suggest-attribute=const

export CFLAGS=-Og -ggdb -pipe -ffreestanding -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse3 -std=gnu23 $(CWARN)

export GDEFINES=-D__KDEBUG__ -DLOG_LEVEL=0 -DENABLE_SERIAL_LOGGING -DSLAB_DEBUG
export TESTS=#-D__PMM_TEST__

# Configure the cross-compiler to use the desired system root.
export SYSROOT="$(shell pwd)/sysroot"
# export CC+=--sysroot=$(SYSROOT) -isystem=$(INCLUDEDIR)

PROJDIRS := helios libc
SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES := $(shell find $(PROJDIRS) -type f -name "*.h")
ALLFILES := $(SRCFILES) $(HDRFILES)

.PHONY: all libc helios clean qemu headers iso bochs todolist limine

all: headers libc helios

helios:
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install

libc:
	@DESTDIR=$(SYSROOT) $(MAKE)  -C ./libc install

headers:
	@mkdir -p $(SYSROOT)
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install-headers

todolist:
	-@grep --color=auto 'TODO.*' -rno $(PROJDIRS)

limine/limine:
	rm -rf limine
	# Download the latest Limine binary release for the 9.x branch.
	git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1
	# Build "limine" utility.
	make -C limine

iso: limine/limine all
	@mkdir -p isodir
	@mkdir -p isodir/boot
	@mkdir -p isodir/boot/limine
	@mkdir -p isodir/EFI/BOOT

	@cp sysroot/boot/$(OSNAME).kernel isodir/boot/$(OSNAME).kernel
	@cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
		  limine/limine-uefi-cd.bin isodir/boot/limine/
	@cp -v limine/BOOTX64.EFI isodir/EFI/BOOT/
	@cp -v limine/BOOTIA32.EFI isodir/EFI/BOOT/
	
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
		-d cpu_reset \
		-D log.txt \
		-serial stdio \
		-enable-kvm \
		-cpu host
		# -nographic \

qemugdb: iso
	qemu-system-$(HOSTARCH) -cdrom $(OSNAME).iso \
		-m 4096M \
		-hdd ./fat.img -boot d -s -S \
		-d cpu_reset \
		-D log.txt \
		-serial stdio \

clean:
	rm -rvf sysroot
	rm -rvf isodir
	rm -rvf *.iso
	$(MAKE) -C ./libc clean
	$(MAKE) -C ./helios clean
	$(MAKE) -C ./limine clean
