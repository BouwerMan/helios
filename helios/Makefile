include $(HELIOS_ROOT)/toolchain.mk

BUILDDIR ?= build
SRC_DIRS := kernel drivers lib mm
ARCHDIR  := arch/$(HOST_ARCH)

include $(ARCHDIR)/config.mk
include ./config.mk

# --- Includes & flags ---------------------------------------------------------
# Our headers use -I so warnings apply; kernel doesn't use sysroot.
K_CPPFLAGS := $(CPPFLAGS) -MMD -MP \
	      -Iinclude -I$(ARCHDIR)/include \
	      -D__KERNEL__ -DHELIOS_KERNEL \
	      $(CPPFLAGS_DEBUG) $(CPPFLAGS_TESTS)

K_CFLAGS   := $(CFLAGS) $(KERNEL_ARCH_CFLAGS) -mcmodel=kernel -mno-red-zone

# Linker: no libc, pull libgcc for compiler builtins if needed.
K_LDFLAGS  := $(LDFLAGS) -nostdlib $(KERNEL_ARCH_LDFLAGS) \
	      -Wl,-Map,$(BUILDDIR)/$(OSNAME).map
K_LDLIBS   := -lgcc $(KERNEL_ARCH_LIBS)

# --- Sources ------------------------------------------------------------------
C_SRCS   := $(sort $(shell find $(SRC_DIRS) -type f -name '*.c'))
ASM_SRCS := $(sort $(shell find $(SRC_DIRS) -type f -name '*.asm'))

C_OBJS   := $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(BUILDDIR)/%.o,$(ASM_SRCS))

OBJS := $(KERNEL_ARCH_OBJS) \
        $(C_OBJS) \
        $(ASM_OBJS) \
        $(BUILDDIR)/fonts/font.o


KERNEL_ELF := $(BUILDDIR)/$(OSNAME).kernel

.PHONY: all clean install install-kernel
.SUFFIXES: .o .c .s .S .asm

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS) $(ARCHDIR)/linker.ld
	@echo "  LD      $@"
	@mkdir -p $(@D)
	$(CC) -T $(ARCHDIR)/linker.ld $(K_LDFLAGS) $(OBJS) -o $@ $(K_LDLIBS)

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	@OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

$(BUILDDIR)/%.o: %.c
	@echo "  CC      $@"
	@mkdir -p $(@D)
	@$(CC) $(K_CPPFLAGS) $(K_CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.asm
	@echo "  AS      $@"
	@mkdir -p $(@D)
	@$(NASM) $(NASMFLAGS) $< -o $@


# Embed PSF font as a binary blob
$(BUILDDIR)/%.o: %.psf
	@mkdir -p $(@D)
	@$(OBJCOPY) -O elf64-x86-64 -B i386 -I binary $< $@

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILDDIR)

install: install-headers install-kernel

install-kernel: $(KERNEL_ELF)
	@echo "Installing kernel..."
	@mkdir -p $(DESTDIR)/$(BOOTDIR)
	@cp $(BUILDDIR)/$(OSNAME).kernel $(DESTDIR)/$(BOOTDIR)

# TODO: install only UAPI headers, not kernel internals.
install-headers:
	@echo "Installing kernel headers..."
	@mkdir -p $(DESTDIR)/usr/include
	@cp include/helios $(DESTDIR)/usr/include -r --preserve=timestamps

# Auto-deps from -MMD -MP
-include $(C_OBJS:.o=.d)
