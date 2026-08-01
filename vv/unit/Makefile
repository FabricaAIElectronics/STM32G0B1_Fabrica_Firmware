# Host unit tests. Built with the NATIVE compiler, not the ARM cross-compiler.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g -I. -Ifakes
BINDIR  := build

TESTS   := test_can_layout test_thermistor_math
BINS    := $(addprefix $(BINDIR)/,$(TESTS))

.PHONY: all clean
all: $(BINS)
	@for b in $(BINS); do ./$$b || exit 1; done

$(BINDIR)/%: %.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< -lm

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR) layouts.json
