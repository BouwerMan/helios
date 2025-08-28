.PHONY: init hello_world all clean

all: init hello_world install

hello_world:
	$(MAKE) -C ./hello_world all

init:
	$(MAKE) -C ./init all

install: init hello_world
	$(MAKE) -C ./init install
	$(MAKE) -C ./hello_world install

clean:
	$(MAKE) -C ./init clean
	$(MAKE) -C ./hello_world clean
