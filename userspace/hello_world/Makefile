LIBS:=$(LIBS) -nostdlib -L$(PWD)/libc/build -lc -lgcc

CFLAGS := -ffreestanding -I$(PWD)/libc/include -static \
	  -fno-stack-protector -fno-pie $(LIBS)

OUT_EXE := hello_world

all:
	@echo "  CC      $@"
	@$(CC) main.c $(CFLAGS) -o $(OUT_EXE)

install: all
	@mkdir -p $(DESTDIR)/usr/bin/
	@cp $(OUT_EXE) $(DESTDIR)/usr/bin/

clean:
	@rm $(OUT_EXE)
