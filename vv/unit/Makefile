# Host unit tests. Built with the NATIVE compiler, not the ARM cross-compiler.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g -I. -Ifakes
BINDIR  := build

TESTS   := test_can_layout test_thermistor_math test_battery_soc
BINS    := $(addprefix $(BINDIR)/,$(TESTS))

# PowerStage sources compiled directly into a test. battery.c includes only
# battery.h, which includes only <stdint.h>/<stdbool.h>, so it builds on the
# host with no HAL stubs at all.
PS_CORE := ../../STM32G0B1_Applciationprog/PowerStage/Core

.PHONY: all clean
all: $(BINS)
	@for b in $(BINS); do ./$$b || exit 1; done

$(BINDIR)/%: %.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< -lm

# Explicit rule wins over the pattern rule above: this one also compiles and
# links the real firmware module, so the assertions test shipped code rather
# than a restatement of it.
$(BINDIR)/test_battery_soc: test_battery_soc.c $(PS_CORE)/Src/battery.c | $(BINDIR)
	$(CC) $(CFLAGS) -I$(PS_CORE)/Inc -o $@ $^ -lm

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR) layouts.json
