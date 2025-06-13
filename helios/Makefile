BUILDDIR=build
SRC_DIRS:=kernel drivers util mm

ARCHDIR=arch/$(HOSTARCH)

CFLAGS:=$(CFLAGS) -isystem$(ARCHDIR)/include  -isysteminclude -isystem$(PWD)/libc/include
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -L$(PWD)/libc/build -lk -lgcc

include $(ARCHDIR)/config.mk
include ./config.mk

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

# 1) collect & sort
C_SRCS   = $(shell find $(SRC_DIRS) -type f -name '*.c')
ASM_SRCS = $(shell find $(SRC_DIRS) -type f -name '*.asm')

# 2) map to object paths
C_OBJS   = $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SRCS))
ASM_OBJS = $(patsubst %.asm,$(BUILDDIR)/%.o,$(ASM_SRCS))

OBJS=\
$(KERNEL_ARCH_OBJS) \
$(C_OBJS) \
$(ASM_OBJS) \
$(BUILDDIR)/fonts/font.o \

LINK_LIST=\
$(LDFLAGS) \
$(OBJS) \
$(LIBS) \

.PHONY: all clean install install-kernel
.SUFFIXES: .o .c .s .S .asm

all: $(OSNAME).kernel

$(OSNAME).kernel: $(OBJS) $(ARCHDIR)/linker.ld
	@echo "Linking kernel..."
	@$(CC) -T $(ARCHDIR)/linker.ld -o $(BUILDDIR)/$@ $(CFLAGS) $(LINK_LIST)

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	@OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

$(BUILDDIR)/%.o: %.c
	@echo "  CC      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(CFLAGS) $(CFLAGS_DEBUG) $(CFLAGS_TESTS)

$(BUILDDIR)/%.o: %.asm
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@nasm -Wall -felf64 -g -F dwarf $< -o $@

$(BUILDDIR)/%.o: %.S
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(CFLAGS) $(CFLAGS_DEBUG) $(CFLAGS_TESTS)

$(BUILDDIR)/%.o: %.s
	@echo "  AR      $@"
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(CFLAGS) $(CFLAGS_DEBUG) $(CFLAGS_TESTS)

$(BUILDDIR)/%.o: %.psf
	@mkdir -p $(@D)
	@objcopy -O elf64-x86-64 -B i386 -I binary $< $@

clean:
	@echo "Cleaning build directory..."
	# @rm $(OBJS)
	@rm -rf $(BUILDDIR)

install: install-kernel

install-kernel: $(OSNAME).kernel
	@mkdir -p $(DESTDIR)$(BOOTDIR)
	@cp $(BUILDDIR)/$(OSNAME).kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
