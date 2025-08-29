export DEFAULT_HOST=x86_64-elf
export HOST?=$(DEFAULT_HOST)
export HOSTARCH=x86_64

export PREFIX=$(CURDIR)/tools/bin
export TARGET=x86_64-elf

export OSNAME=HeliOS

export AR:=$(PREFIX)/$(HOST)-ar
export AS:=$(PREFIX)/$(HOST)-as
export CC:=$(PREFIX)/$(HOST)-gcc

TOOLCHAIN_BINS := $(AR) $(AS) $(CC)

# Use 'wildcard' to find which of the required tools actually exist
EXISTING_TOOLS := $(wildcard $(TOOLCHAIN_BINS))

# Compare the list of required tools with the list of existing ones.
# If they don't match, something is missing.
ifneq ($(TOOLCHAIN_BINS),$(EXISTING_TOOLS))
    # For a better error message, figure out exactly which tools are missing
    MISSING_TOOLS := $(filter-out $(EXISTING_TOOLS),$(TOOLCHAIN_BINS))
    # Stop make and print a descriptive error
    $(error Missing required tools: $(MISSING_TOOLS). Please check your cross-compiler installation and PREFIX.)
endif

$(info Using cross-compiler: $(CC))

export PREFIX=/usr
export EXEC_PREFIX=$(PREFIX)
export BOOTDIR=/boot
export LIBDIR=$(EXEC_PREFIX)/lib
export INCLUDEDIR=$(EXEC_PREFIX)/include

export CWARN=-Wall -Wextra -pedantic -Wshadow -Wpointer-arith \
	-Wwrite-strings \
        -Wredundant-decls -Wnested-externs -Wno-long-long \
        -Wconversion -Wstrict-prototypes \
	# -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wno-error=suggest-attribute=pure -Wno-error=suggest-attribute=const

export CFLAGS=-Og -ggdb -pipe -ffreestanding -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -mno-mmx -mno-sse -mno-sse3 -std=gnu23 $(CWARN)

export GDEFINES=-DLOG_LEVEL=0 -DENABLE_SERIAL_LOGGING

# Configure the cross-compiler to use the desired system root.
export SYSROOT=$(CURDIR)/sysroot
export CC+=--sysroot=$(SYSROOT) -isystem=$(INCLUDEDIR)

PROJDIRS := helios libc
SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES := $(shell find $(PROJDIRS) -type f -name "*.h")
ALLFILES := $(SRCFILES) $(HDRFILES)

.PHONY: all libc helios clean qemu headers iso bochs todolist limine userspace

all: headers libc userspace helios

helios:
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install

libc: headers
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install-headers
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install

userspace: libc
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./userspace all


headers:
	@mkdir -p $(SYSROOT)
	# @DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install-headers
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install-headers

todolist:
	-@grep --color=auto 'TODO.*' -rno $(PROJDIRS)

INITRAMFS_DIR := $(CURDIR)/isodir/boot
INITRAMFS_TAR := $(INITRAMFS_DIR)/initramfs.tar

initramfs: all
	@echo "Creating initramfs..."
	@mkdir -p $(INITRAMFS_DIR)
# Create a tar archive from the contents of sysroot
	tar -v --transform 's,^\./,/,' -cf $(INITRAMFS_TAR) -C $(SYSROOT) .


limine/limine:
	# Download the latest Limine binary release for the 9.x branch.
	if ! [ -d "limine" ]; then git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1; fi
	# Build "limine" utility.
	make -C limine

iso: limine/limine initramfs
	@mkdir -p isodir/boot/limine isodir/EFI/BOOT
	@cp sysroot/* isodir/ -r
	# @cp sysroot/boot/$(OSNAME).kernel isodir/boot/
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
		-no-reboot \
		-serial stdio \
		-enable-kvm \
		-cpu host \
		# -display none

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
	@$(MAKE) -C ./userspace clean
