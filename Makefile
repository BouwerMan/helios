HELIOS_ROOT ?= $(shell git rev-parse --show-toplevel 2>/dev/null || echo $(CURDIR))
export HELIOS_ROOT
include $(HELIOS_ROOT)/toolchain.mk

# TODO: Better way to check for cross-compiler

$(info Using cross-compiler: $(CC))

PROJDIRS := helios libc
SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES := $(shell find $(PROJDIRS) -type f -name "*.h")
ALLFILES := $(SRCFILES) $(HDRFILES)

# boot d = boot from cdrom
QEMU_MOUNTS := -cdrom $(OSNAME).iso -boot d 
QEMU_MEM    := -m 4096M
QEMU_FLAGS  := $(QEMU_MEM) $(QEMU_MOUNTS) -no-reboot -d cpu_reset -D cpu_reset_log.txt \
	       -device isa-debug-exit,iobase=0xF4,iosize=0x04

ifeq ($(QEMU_USE_DEBUGCON), 1)
	QEMU_FLAGS += -debugcon stdio
else
	QEMU_FLAGS += -serial stdio
endif

OVMF_SOURCE_URL := https://retrage.github.io/edk2-nightly/bin
OVMF_LOCAL_PATH := ovmf
OVMF_CODE := $(OVMF_LOCAL_PATH)/RELEASEX64_OVMF_CODE.fd
OVMF_VARS := $(OVMF_LOCAL_PATH)/RELEASEX64_OVMF_VARS.fd

QEMU_UEFI := -drive if=pflash,format=raw,unit=0,file=$(OVMF_CODE),readonly=on \
	     -drive if=pflash,format=raw,unit=1,file=$(OVMF_VARS) \
	     -net none


.PHONY: all libc helios qemu headers iso todolist limine userspace compile_commands tidy ovmf docs

all: headers helios libc userspace

helios:
	@echo "Building Helios kernel..."
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install

libc: headers
	@echo "Building Helios libc..."
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install

userspace: libc
	@echo "Building Helios userspace..."
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./userspace install

headers:
	@echo "Installing headers..."
	@mkdir -p $(SYSROOT)
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./helios install-headers
	@DESTDIR=$(SYSROOT) $(MAKE) -C ./libc install-headers

docs:
	@echo "Generating documentation..."
	@$(MAKE) -C ./docs

docs-index:
	@$(MAKE) -C ./docs index

docs-preview:
	@$(MAKE) -C ./docs preview

docs-clean:
	@$(MAKE) -C ./docs clean

todolist:
	-@grep --color=auto 'TODO.*' -rno $(PROJDIRS)

compile_commands:
	@command -v bear >/dev/null || { echo "error: 'bear' not found. Install it first."; exit 1; }
	@rm -f compile_commands.json
# Force recompiles so Bear sees every compile invocation across sub-makes.
	@bear --output compile_commands.json -- $(MAKE) -B all
	@echo "Wrote compile_commands.json"

TIDY_FILES := $(shell git ls-files '*.c')

tidy:
	@test -f compile_commands.json || (echo "error: compile_commands.json not found. Run 'make compile_commands' first."; exit 1)
	@echo "Running clang-tidy on $(words $(TIDY_FILES)) files"
	@echo "$(TIDY_FILES)" | xargs -n1 -P$$(nproc) clang-tidy -p . --quiet

INITRAMFS_DIR := $(CURDIR)/sysroot/boot
INITRAMFS_TAR := $(INITRAMFS_DIR)/initramfs.tar

initramfs: all
	@echo "Creating initramfs..."
	@mkdir -p $(INITRAMFS_DIR)
	@tar --transform 's,^\./,/,' -cf $(INITRAMFS_TAR) -C $(SYSROOT) .

limine/limine:
	@if ! [ -d "limine" ]; then git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1; fi
	@$(MAKE) -C limine

iso: limine/limine initramfs
	@echo "Creating bootable ISO..."
	@mkdir -p $(SYSROOT)/boot/limine $(SYSROOT)/EFI/BOOT
	@cp limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
		  limine/limine-uefi-cd.bin $(SYSROOT)/boot/limine/
	@cp limine/BOOTX64.EFI limine/BOOTIA32.EFI $(SYSROOT)/EFI/BOOT/
	
# Create the bootable ISO.
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			$(SYSROOT) -o $(OSNAME).iso
# Install Limine stage 1 and 2 for legacy BIOS boot.
	@./limine/limine bios-install $(OSNAME).iso

$(OVMF_VARS):
	@mkdir -p $(OVMF_LOCAL_PATH)
	@echo "Downloading OVMF VARS..."
	@curl -fsSL --progress-bar -o $@ \
		$(OVMF_SOURCE_URL)/$(notdir $@)

$(OVMF_CODE):
	@mkdir -p $(OVMF_LOCAL_PATH)
	@echo "Downloading OVMF CODE..."
	@curl -fsSL --progress-bar -o $@ \
		$(OVMF_SOURCE_URL)/$(notdir $@)

ovmf: $(OVMF_CODE) $(OVMF_VARS)

qemu: iso ovmf
	$(QEMU) $(QEMU_FLAGS) $(QEMU_UEFI) -enable-kvm -cpu host,migratable=no,+invtsc,hv_time,hv_frequencies

gdbinit:
	@echo "Generating .gdbinit..."
	./scripts/gen_gdbinit.sh

qemugdb: iso gdbinit ovmf
	$(QEMU) $(QEMU_FLAGS) $(QEMU_UEFI) -s -S -cpu qemu64,tsc-frequency=3609600000,+invtsc,vmware-cpuid-freq=on

.PHONY: clean distclean pristine clean-docs clean-iso

CLEAN_DIRS  := $(SYSROOT)
CLEAN_FILES := *.iso ./cpu_reset_log.txt ./gdb.txt

DISTCLEAN_DIRS  := ./ovmf ./limine
DISTCLEAN_FILES :=

PRISTINE_DIRS  := ./.cache
PRISTINE_FILES := ./compile_commands.json ./.gdbinit

clean:
	-@$(MAKE) -C ./libc clean
	-@$(MAKE) -C ./helios clean
	-@$(MAKE) -C ./userspace clean
	-@rm -rf $(CLEAN_DIRS)
	-@rm -f $(CLEAN_FILES)

distclean: clean docs-clean
	-@$(MAKE) -C ./limine clean
	-@rm -rf $(DISTCLEAN_DIRS)
	-@rm -f $(DISTCLEAN_FILES)

pristine: distclean
	-@rm -rf $(PRISTINE_DIRS)
	-@rm -f $(PRISTINE_FILES)
