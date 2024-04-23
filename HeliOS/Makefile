CFLAGS?=-O0 -g -std=gnu17
CPPFLAGS?=
LDFLAGS?=
LIBS?=

OSNAME?=myos
BUILDDIR=build
KERNELDIR=kernel

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_kernel -Iinclude
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lk -lgcc

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

DEFINES:=
# DEFINES+=-DKERNEL_DEBUG
DEFINES+=-DKMALLOC_TESTING
# DEFINES+=-DMEM_MAP_DUMP
# DEFINES+=-DPRINTF_TESTING

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
$(BUILDDIR)/$(KERNELDIR)/kernel.o \
$(BUILDDIR)/$(KERNELDIR)/gdt.o \
$(BUILDDIR)/$(KERNELDIR)/idt.o \
$(BUILDDIR)/$(KERNELDIR)/isr.o \
$(BUILDDIR)/$(KERNELDIR)/irq.o \
$(BUILDDIR)/$(KERNELDIR)/timer.o \
$(BUILDDIR)/$(KERNELDIR)/keyboard.o \
$(BUILDDIR)/$(KERNELDIR)/panic.o \
$(BUILDDIR)/$(KERNELDIR)/cpu.o \
$(BUILDDIR)/$(KERNELDIR)/memory.o \
$(BUILDDIR)/$(KERNELDIR)/liballoc.o \
$(BUILDDIR)/$(KERNELDIR)/liballoc_hooks.o \
$(BUILDDIR)/$(KERNELDIR)/pci/pci.o \
$(BUILDDIR)/$(KERNELDIR)/ata/controller.o \
$(BUILDDIR)/$(KERNELDIR)/ata/device.o \
$(BUILDDIR)/$(KERNELDIR)/ata/ata.o \
$(BUILDDIR)/$(KERNELDIR)/ata/partition.o \
$(BUILDDIR)/$(KERNELDIR)/fs/fat.o \
$(BUILDDIR)/$(KERNELDIR)/fs/vfs.o \

OBJS=\
$(KERNEL_OBJS) \
#$(ARCHDIR)/crti.o \
#$(ARCHDIR)/crtbegin.o \
#$(ARCHDIR)/crtend.o \
#$(ARCHDIR)/crtn.o \

LINK_LIST=\
$(LDFLAGS) \
$(KERNEL_OBJS) \
$(LIBS) \
#$(ARCHDIR)/crti.o \
#$(ARCHDIR)/crtbegin.o \
#$(ARCHDIR)/crtend.o \
#$(ARCHDIR)/crtn.o \


.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .s .asm

all: $(OSNAME).kernel

$(OSNAME).kernel: $(OBJS) $(ARCHDIR)/linker.ld
	$(CC) -T $(ARCHDIR)/linker.ld -o $(BUILDDIR)/$@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot $(BUILDDIR)/$(OSNAME).kernel

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

$(BUILDDIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS) $(DEFINES)

$(BUILDDIR)/%.o: %.s
	@mkdir -p $(@D)
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

$(BUILDDIR)/%.o: %.asm
	@mkdir -p $(@D)
	nasm -felf32 $< -o $@

clean:
	rm -rvf $(BUILDDIR)

install: install-headers install-kernel

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-kernel: $(OSNAME).kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp $(BUILDDIR)/$(OSNAME).kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
