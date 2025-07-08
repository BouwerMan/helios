BUILDDIR=build

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
INCLUDEDIR?=$(PREFIX)/include
LIBDIR?=$(EXEC_PREFIX)/lib

ARCHDIR=arch/$(HOSTARCH)
include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(ARCH_CFLAGS) -ffreestanding -Wall -Wextra -pedantic -Werror -isystem$(ARCHDIR)/include -isysteminclude
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS) -isysteminclude -isystem$(ARCHDIR)/include

LIBK_CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS) -I$(PWD)/helios/include -D__is_libk
LIBK_CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS) -D__is_libk

LIBC_CFLAGS:=$(CFLAGS) -D__is_libc
LIBC_CPP_FLAGS:=$(CPP_FLAGS) -D__is_libc

SRC_DIRS:=ctype stdio stdlib string $(ARCHDIR)

# 1) collect & sort
C_SRCS   = $(shell find $(SRC_DIRS) -type f -name '*.c')
ASM_SRCS = $(shell find $(SRC_DIRS) -type f -name '*.asm')

# 2) map to object paths
C_OBJS   = $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SRCS))
ASM_OBJS = $(patsubst %.asm,$(BUILDDIR)/%.o,$(ASM_SRCS))

FREEOBJS=\
$(ARCH_FREEOBJS) \
$(C_OBJS) \
$(ASM_OBJS)

HOSTEDOBJS=\
$(ARCH_HOSTEDOBJS) \

OBJS=\
$(FREEOBJS) \
$(HOSTEDOBJS) \
$(BUILDDIR)/crt0.o

LIBK_OBJS=$(FREEOBJS:.o=.libk.o)
LIBC_OBJS=$(OBJS:.o=.libc.o)

BINARIES=$(BUILDDIR)/libc.a $(BUILDDIR)/libk.a # Not ready for libc yet.

.PHONY: all clean install install-headers install-libs
.SUFFIXES: .o .libk.o .libc.o .c .S .asm

all: $(BINARIES)

$(BUILDDIR)/libc.a: $(LIBC_OBJS)
	@$(AR) rcs $@ $(LIBC_OBJS)

$(BUILDDIR)/libk.a: $(LIBK_OBJS)
	@$(AR) rcs $@ $(LIBK_OBJS)

$(BUILDDIR)/%.o: %.c
	@echo "  CC      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $(BUILDDIR)/$@ $(CFLAGS) $(CPPFLAGS)

.c.S:
	@$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

$(BUILDDIR)/%.libk.o: %.asm
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@nasm -Wall -g -F dwarf -felf64 $< -o $@

$(BUILDDIR)/%.libc.o: %.asm
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@nasm -Wall -g -F dwarf -felf64 $< -o $@

$(BUILDDIR)/%.libk.o: %.c
	@echo "  CC      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(LIBK_CFLAGS) $(LIBK_CPPFLAGS)

$(BUILDDIR)/%.libc.o: %.c
	@echo "  CC      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(LIBC_CFLAGS) $(LIBC_CPPFLAGS)

.S.libk.o:
	@$(CC) -MD -c $< -o $@ $(LIBK_CFLAGS) $(LIBK_CPPFLAGS)

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILDDIR)
	
install: install-headers install-libs

install-headers:
	@mkdir -p $(DESTDIR)$(INCLUDEDIR)
	@cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

# TODO: Maybe don't install libk
install-libs: $(BINARIES)
	@mkdir -p $(DESTDIR)$(LIBDIR)
	@cp $(BINARIES) $(DESTDIR)$(LIBDIR)

-include $(OBJS:.o=.d)
-include $(LIBK_OBJS:.o=.d)
