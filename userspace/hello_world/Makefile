LIBS:=$(LIBS) -nostdlib -L$(PWD)/libc/build -lc -lgcc

CFLAGS := -ffreestanding -I$(PWD)/libc/arch/x86_64/include -I$(PWD)/libc/include -static \
	  -fno-stack-protector -fno-pie $(LIBS) -mgeneral-regs-only -mno-red-zone -mno-mmx -mno-sse -mno-sse3 -ggdb \
	  -I$(SYSROOT)/usr/include

OUT_EXE := hello_world.elf

all:
	@echo "  CC      $@"
	@$(CC) main.c $(CFLAGS) -o $(OUT_EXE)

install: all
	@mkdir -p $(DESTDIR)/usr/bin/
	@cp $(OUT_EXE) $(DESTDIR)/usr/bin/

clean:
	@rm $(OUT_EXE)
