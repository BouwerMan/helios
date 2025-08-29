HELIOS_ROOT ?= $(shell git rev-parse --show-toplevel 2>/dev/null || echo $(CURDIR))
export HELIOS_ROOT
include $(HELIOS_ROOT)/toolchain.mk

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

REQUIRED_PATH_TOOLS := git xorriso bear $(NASM) $(QEMU)

# Helper: returns the tool name if not found
define _missing_if_not_on_path
$(if $(shell command -v $(1) >/dev/null 2>&1 && echo found),,$(1))
endef

MISSING_PATH_TOOLS := $(strip $(foreach t,$(REQUIRED_PATH_TOOLS),$(call _missing_if_not_on_path,$(t))))
ifneq ($(MISSING_PATH_TOOLS),)
  $(error Missing required PATH tools: $(MISSING_PATH_TOOLS))
endif

$(info "Using cross-compiler: $(CC)")

PROJDIRS := helios libc
SRCFILES := $(shell find $(PROJDIRS) -type f -name "*.c")
HDRFILES := $(shell find $(PROJDIRS) -type f -name "*.h")
ALLFILES := $(SRCFILES) $(HDRFILES)

.PHONY: all libc helios clean qemu headers iso todolist limine userspace compile_commands

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

todolist:
	-@grep --color=auto 'TODO.*' -rno $(PROJDIRS)

compile_commands:
	@command -v bear >/dev/null || { echo "error: 'bear' not found. Install it first."; exit 1; }
	@rm -f compile_commands.json
# Force recompiles so Bear sees every compile invocation across sub-makes.
	@bear --output compile_commands.json -- $(MAKE) -B all
	@echo "Wrote compile_commands.json"

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
	@cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
		  limine/limine-uefi-cd.bin $(SYSROOT)/boot/limine/
	@cp -v limine/BOOTX64.EFI limine/BOOTIA32.EFI $(SYSROOT)/EFI/BOOT/
	
# Create the bootable ISO.
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
			-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			$(SYSROOT) -o $(OSNAME).iso
# Install Limine stage 1 and 2 for legacy BIOS boot.
	@./limine/limine bios-install $(OSNAME).iso

qemu: iso
	$(QEMU) -cdrom $(OSNAME).iso \
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
	@echo "Generating .gdbinit..."
	./scripts/gen_gdbinit.sh

qemugdb: iso gdbinit
	$(QEMU) -cdrom $(OSNAME).iso \
		-m 4096M \
		-hdd ./fat.img -boot d -s -S \
		-d cpu_reset \
		-D log.txt \
		-serial stdio

clean:
	-@$(MAKE) -C ./libc clean
	-@$(MAKE) -C ./helios clean
	-@$(MAKE) -C ./limine clean
	-@$(MAKE) -C ./userspace clean
	-@rm -rf sysroot
	-@rm -rf *.iso
	-@rm .gdbinit
