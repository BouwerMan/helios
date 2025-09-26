include $(HELIOS_ROOT)/toolchain.mk

# NOTE: REPLACE THESE WITH YOUR APPLICATION NAME
APP      := hsh
APP_ELF  := $(APP).elf
BUILDDIR ?= build

SRCS  := $(sort $(shell find * -type f \( -name "*.c" \
				       -o -name "*.asm" \
				       -o -name "*.S" \)))

OBJS  := $(patsubst %.c,$(BUILDDIR)/%.o,$(filter %.c,$(SRCS))) \
	 $(patsubst %.S,$(BUILDDIR)/%.o,$(filter %.S,$(SRCS))) \
	 $(patsubst %.asm,$(BUILDDIR)/%.o,$(filter %.asm,$(SRCS)))

APP_LDLIBS :=

.PHONY: all install clean
all: $(APP_ELF)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(US_CFLAGS) $(US_CPPFLAGS)

$(BUILDDIR)/%.o: %.S
	@mkdir -p $(@D)
	@$(CC) -MD -c $< -o $@ $(US_CFLAGS) $(US_CPPFLAGS)

$(BUILDDIR)/%.o: %.asm
	@mkdir -p $(@D)
	@$(NASM) $(NASMFLAGS) $< -o $@

# Link order: CRT prologue, objects, libs, CRT epilogue
$(APP_ELF): $(OBJS) $(LIBC_DEPS)
	@$(CC) $(US_LDFLAGS) $(CRT0) $(CRTI) $(OBJS) -o $@ $(US_LDLIBS) $(APP_LDLIBS) $(CRTN)

install: $(APP_ELF)
	@mkdir -p $(SYSROOT)/usr/bin
	@cp $(APP_ELF) $(SYSROOT)/usr/bin/

clean:
	-@rm -rf $(BUILDDIR) $(APP_ELF)

-include $(OBJS:.o=.d)
