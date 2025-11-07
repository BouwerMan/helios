# For now I am just going to put output in docs/build only.
# In the future we can either install into system maybe?
# Or also install into sysroot (once it's not all in initramfs)

.PHONY: all docs clean docs preview

PANDOC ?= pandoc
PANFLAGS ?= -s -t man

DOC_ROOT ?= .
BUILD_DIR ?= build

MAN_SECTIONS := 1 2 3 4 5 7 8 9

SRC_DIRS := $(addprefix $(DOC_ROOT)/man,$(MAN_SECTIONS))

# Collect *.N.md pages (flat or nested)
MAN_SRCS := $(strip $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.[1-9].md)))

# Map "man2/getdents.2.md" -> "build/man2/getdents.2"
OUT_DIR := $(BUILD_DIR)
MAN_PAGES := $(patsubst $(DOC_ROOT)/%.md,$(OUT_DIR)/%, $(MAN_SRCS))

# Hard fail early if pandoc isn't available
ifeq (,$(shell command -v $(PANDOC) 2>/dev/null))
$(error "pandoc not found; install it or pass PANDOC=/path/to/pandoc")
endif

all: docs
docs: $(MAN_PAGES)

$(OUT_DIR)/%: $(DOC_ROOT)/%.md
	@mkdir -p $(@D)
	@$(PANDOC) $(PANFLAGS) $< -o $@
	@printf '  \033[32mMAN\033[0m     %s\n' $@

index: docs
	@for s in $(MAN_SECTIONS); do \
	  d="$(OUT_DIR)/man$$s"; \
	  if [ -d "$$d" ]; then \
	    echo "man$$s:"; ls -1 "$$d" | sed 's/^/  /'; echo; \
	  fi; \
	done

# Quick preview in terminal:
#   make preview SEC=9 NAME=vfs_readdir
preview: docs
	@if [ -z "$(SEC)" ] || [ -z "$(NAME)" ]; then \
	  echo "usage: make preview SEC=<section> NAME=<page>"; exit 2; \
	fi
	@MANPATH=$(abspath $(OUT_DIR)) man $(SEC) $(NAME)

clean:
	@rm -rf $(BUILD_DIR)
