BUILDDIR=build

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
INCLUDEDIR?=$(PREFIX)/include
LIBDIR?=$(EXEC_PREFIX)/lib

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra -pedantic -Werror
CPPFLAGS:=$(CPPFLAGS) -D__is_libc -Iinclude
LIBK_CFLAGS:=$(CFLAGS) -I$(PWD)/helios/include
LIBK_CPPFLAGS:=$(CPPFLAGS) -D__is_libk

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(ARCH_CPPFLAGS)
LIBK_CFLAGS:=$(LIBK_CFLAGS) $(KERNEL_ARCH_CFLAGS)
LIBK_CPPFLAGS:=$(LIBK_CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)

SRC_DIRS:=ctype stdio stdlib string

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

LIBK_OBJS=$(FREEOBJS:.o=.libk.o)

#BINARIES=libc.a libk.a # Not ready for libc yet.
BINARIES=libk.a

.PHONY: all clean install install-headers install-libs
.SUFFIXES: .o .libk.o .c .S .asm

all: $(BINARIES)

libc.a: $(OBJS)
	@$(AR) rcs $@ $(OBJS)

libk.a: $(LIBK_OBJS)
	@$(AR) rcs $(BUILDDIR)/$@ $(LIBK_OBJS)

$(BUILDDIR)/%.o: %.c
	@echo "  CC      $@"
	@$(CC) -MD -c $< -o $(BUILDDIR)/$@ $(CFLAGS) $(CPPFLAGS)

.c.S:
	@$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

$(BUILDDIR)/%.o: %.asm
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@nasm -felf32 $< -o $@

$(BUILDDIR)/%.libk.o: %.c
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(LIBK_CFLAGS) $(LIBK_CPPFLAGS)

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
	@cp $(BUILDDIR)/$(BINARIES) $(DESTDIR)$(LIBDIR)

-include $(OBJS:.o=.d)
-include $(LIBK_OBJS:.o=.d)

