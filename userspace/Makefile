# Discover app dirs (immediate subdirs that have a Makefile)
APP_DIRS := $(sort $(patsubst %/,%,$(dir $(wildcard */Makefile))))
APPS     := $(notdir $(APP_DIRS))
$(info "Found apps: $(APPS)")

# Derived targets
APP_INSTALLS := $(addsuffix .install,$(APPS))
APP_CLEANS   := $(addsuffix .clean,$(APPS))

# Mark them all phony (variable expansion is fine)
.PHONY: all apps $(APPS) $(APP_INSTALLS) $(APP_CLEANS) clean install

all: apps
apps: $(APPS)

# Build a single app by name (e.g., `make init`)
$(APPS):
	$(MAKE) -C $@ all

# Install all apps into the sysroot (or DESTDIR you pass in)
install: $(APP_INSTALLS)
$(APP_INSTALLS):
	@d=$(@:.install=); $(MAKE) -C $$d install

# Clean all apps
clean: $(APP_CLEANS)
$(APP_CLEANS):
	@d=$(@:.clean=); $(MAKE) -C $$d clean
