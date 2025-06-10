export DEFAULT_HOST=x86_64-elf
export HOST?=$(DEFAULT_HOST)
export HOSTARCH=x86_64

export PREFIX=$(CURDIR)/tools/bin
export TARGET=x86_64-elf

export OSNAME=HeliOS

export AR:=$(PREFIX)/$(HOST)-ar
export AS:=$(PREFIX)/$(HOST)-as
export CC:=$(PREFIX)/$(HOST)-gcc

$(info Using cross-compiler: $(CC))

export PREFIX=/usr
export EXEC_PREFIX=$(PREFIX)
export BOOTDIR=/boot
export LIBDIR=$(EXEC_PREFIX)/lib
export INCLUDEDIR=$(EXEC_PREFIX)/include

export CWARN=-Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
	-Wwrite-strings \
        -Wredundant-decls -Wnested-externs -Wno-long-long \
        -Wconversion -Wstrict-prototypes \
	# -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wno-error=suggest-attribute=pure -Wno-error=suggest-attribute=const

export CFLAGS=-Og -ggdb -pipe -ffreestanding -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -mno-mmx -mno-sse -mno-sse3 -std=gnu23 $(CWARN)

export GDEFINES=-D__KDEBUG__ -DLOG_LEVEL=0 -DENABLE_SERIAL_LOGGING -DSLAB_DEBUG

# Configure the cross-compiler to use the desired system root.
export SYSROOT=$(CURDIR)/sysroot
export CC+=--sysroot=$(SYSROOT) -isystem=$(INCLUDEDIR)

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
	# Download the latest Limine binary release for the 9.x branch.
	if ! [ -d "limine" ]; then git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1; fi
	# Build "limine" utility.
	make -C limine

iso: limine/limine all
	@mkdir -p isodir/boot/limine isodir/EFI/BOOT
	@cp sysroot/boot/$(OSNAME).kernel isodir/boot/
	@cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
		  limine/limine-uefi-cd.bin isodir/boot/limine/
	@cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI isodir/EFI/BOOT/
	# Create the bootable ISO.
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			isodir -o $(OSNAME).iso
	# Install Limine stage 1 and 2 for legacy BIOS boot.
	@./limine/limine bios-install $(OSNAME).iso

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

gdbinit:
	./scripts/gen_gdbinit.sh

qemugdb: iso gdbinit
	qemu-system-$(HOSTARCH) -cdrom $(OSNAME).iso \
		-m 4096M \
		-hdd ./fat.img -boot d -s -S \
		-d cpu_reset \
		-D log.txt \
		-serial stdio

clean:
	-@rm -rf sysroot
	-@rm -rf isodir
	-@rm -rf *.iso
	-@rm .gdbinit
	@$(MAKE) -C ./libc clean
	@$(MAKE) -C ./helios clean
	@$(MAKE) -C ./limine clean
