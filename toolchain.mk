ifndef __HELIOS_TOOLCHAIN_MK__
__HELIOS_TOOLCHAIN_MK__ := 1

# Detect repo root (works both from subdirs and top-level)
HELIOS_ROOT ?= $(shell git rev-parse --show-toplevel 2>/dev/null || echo $(CURDIR)/../..)

OSNAME ?= HeliOS

QEMU_USE_DEBUGCON ?= 1

# Cross tools, can't be overridden otherwise normal linux tools will be used
HOST_ARCH := x86_64
HOST 	  := $(HOST_ARCH)-elf
AR 	  := $(HELIOS_ROOT)/tools/bin/$(HOST)-ar
AS 	  := $(HELIOS_ROOT)/tools/bin/$(HOST)-as
CC 	  := $(HELIOS_ROOT)/tools/bin/$(HOST)-gcc
OBJCOPY   := $(HELIOS_ROOT)/tools/bin/$(HOST)-objcopy
OBJDUMP   := $(HELIOS_ROOT)/tools/bin/$(HOST)-objdump
READELF   := $(HELIOS_ROOT)/tools/bin/$(HOST)-readelf
STRIP     := $(HELIOS_ROOT)/tools/bin/$(HOST)-strip
NASM      := nasm
QEMU      := qemu-system-$(HOST_ARCH)

# Sysroot
SYSROOT    ?= $(HELIOS_ROOT)/sysroot
INCLUDEDIR ?= /usr/include
LIBDIR     ?= /usr/lib
BUILDDIR   ?= build
BOOTDIR    ?= /boot

COMMON_WARN   := -Wall -Wextra -Wshadow -Wpointer-arith \
	         -Wwrite-strings \
                 -Wredundant-decls -Wnested-externs -Wno-long-long \
                 -Wconversion -Wstrict-prototypes

COMMON_CFLAGS := -std=gnu23 -ggdb -pipe -ffreestanding \
		 -fno-omit-frame-pointer -fno-stack-protector -fno-pie \
		 -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse3

ARFLAGS   := rcsD
NASMFLAGS := -f elf64 -g -F dwarf -Wall -w+orphan-labels -w-reloc-rel-dword

LDFLAGS := -nostdlib -no-pie
LDLIBS := -lgcc

# Start files
CRT0 := $(SYSROOT)/$(LIBDIR)/crt0.o
CRTI := $(SYSROOT)/$(LIBDIR)/crti.o
CRTN := $(SYSROOT)/$(LIBDIR)/crtn.o

OPT ?= -Og
PIC ?=

CFLAGS   := $(OPT) $(COMMON_CFLAGS) $(COMMON_WARN)
CPPFLAGS := -MMD -MP

# Installed libc components that userspace depends on
LIBC_DEPS := $(SYSROOT)/$(LIBDIR)/libc.a $(CRT0) $(CRTI) $(CRTN)

# Userspace
US_CPPFLAGS := $(CPPFLAGS) --sysroot=$(SYSROOT) -isystem$(SYSROOT)/$(INCLUDEDIR)
US_CFLAGS   := $(CFLAGS) $(PIC)
US_LDFLAGS  := $(LDFLAGS) --sysroot=$(SYSROOT) -L$(SYSROOT)/$(LIBDIR) -static
US_LDLIBS   := $(LDLIBS) -lc

endif
