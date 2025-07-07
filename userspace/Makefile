
.PHONY: hello_world all clean

all: hello_world install

hello_world:
	$(MAKE) -C ./hello_world all

install: hello_world
	$(MAKE) -C ./hello_world install

clean:
	$(MAKE) -C ./hello_world clean
