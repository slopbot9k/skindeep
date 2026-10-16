# The C tree of decision 0036. It needs GNU make 3.81 or later, which
# macOS ships as /usr/bin/make and Linux distributions ship as make, and
# the system C compiler.
#
#   make              build the library, the tests, skinned-relay and hookdump
#   make test         run the tests (T=json runs the json suite)
#   make debug        run the tests under AddressSanitizer and UBSan
#   make fuzz         build the fuzz targets
#   make fuzz-run     run each target for FUZZ_SECONDS (default 30)
#   make clean
#
# MODE picks the build: release (the default), debug or fuzz, each in
# build/<mode>/.

OS := $(shell uname -s)
MODE ?= release
BUILD := build/$(MODE)

CSTD := -std=c17
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
        -Wmissing-prototypes -Wvla -Wformat=2 -Wundef -Wpointer-arith -Werror
DEFS := -D_XOPEN_SOURCE=700
# Apple's headers hide parts of POSIX.1-2008, such as mkdtemp, unless
# this is set. Linux keeps the strict view, so CI there still catches
# anything outside POSIX.
ifeq ($(OS),Darwin)
DEFS += -D_DARWIN_C_SOURCE
endif
LDLIBS := -lpthread -lm
SANITIZE := -fsanitize=address,undefined -fno-sanitize-recover=undefined

ifeq ($(MODE),release)
OPT = -O2 -g
endif
ifeq ($(MODE),debug)
OPT = -O1 -g -fno-omit-frame-pointer $(SANITIZE)
LDFLAGS += $(SANITIZE)
endif
ifeq ($(MODE),fuzz)
# Coverage guides the in-tree mutator where the compiler can instrument
# for it, as clang can; elsewhere it mutates blind.
CC := $(or $(FUZZ_CC),$(CC))
COVERAGE_OK := $(shell echo 'int x;' | $(CC) -fsanitize-coverage=trace-pc-guard -x c -c - -o /dev/null 2>/dev/null && echo yes)
COVERAGE := $(if $(COVERAGE_OK),-fsanitize-coverage=trace-pc-guard,)
OPT = -O1 -g -fno-omit-frame-pointer $(SANITIZE) $(COVERAGE)
LDFLAGS += $(SANITIZE)
endif

COMPILE = $(CC) $(CSTD) $(WARN) $(DEFS) $(OPT) $(CFLAGS) -MMD -MP

# Code for one system lives in src/platform/<os>/ (decision 0036).
PLATFORM := $(if $(filter Darwin,$(OS)),macos,linux)

LIB_SRC := $(wildcard src/base/*.c src/core/*.c src/agents/*.c src/agents/*/*.c src/sessions/*.c \
    src/platform/$(PLATFORM)/*.c) src/relay/relay.c
LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o)
LIB := $(BUILD)/libsk.a

TEST_SRC := $(filter-out tests/fuzz/%,$(wildcard tests/*.c tests/*/*.c))
TEST_OBJ := $(TEST_SRC:%.c=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/tests/run

RELAY := $(BUILD)/skinned-relay
HOOKDUMP := $(BUILD)/hookdump

FUZZ_TARGETS := json http utf8 base64
FUZZ_BINS := $(FUZZ_TARGETS:%=$(BUILD)/fuzz-%)
FUZZ_DRIVER := $(BUILD)/tests/fuzz/driver.o
FUZZ_SECONDS ?= 30

.PHONY: all test debug fuzz fuzz-run clean
.SECONDARY:

all: $(LIB) $(TEST_BIN) $(RELAY) $(HOOKDUMP)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE) -c $< -o $@

# The driver holds the coverage callbacks, so it is not instrumented.
$(FUZZ_DRIVER): COVERAGE :=

$(LIB): $(LIB_OBJ)
	@rm -f $@
	$(AR) rcs $@ $(LIB_OBJ)

$(TEST_BIN): $(TEST_OBJ) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $(TEST_OBJ) $(LIB) $(LDLIBS)

$(RELAY): $(BUILD)/src/relay/main.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(HOOKDUMP): $(BUILD)/tools/hookdump.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(TEST_BIN) $(RELAY)
	SK_RELAY_BIN=$(RELAY) ./$(TEST_BIN) $(T)

debug:
	$(MAKE) MODE=debug test

$(BUILD)/fuzz-%: $(BUILD)/tests/fuzz/%.o $(FUZZ_DRIVER) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

ifeq ($(MODE),fuzz)
fuzz: $(FUZZ_BINS)

fuzz-run: $(FUZZ_BINS)
	@for t in $(FUZZ_TARGETS); do \
	    echo "fuzz $$t for $(FUZZ_SECONDS) s"; \
	    $(BUILD)/fuzz-$$t -t $(FUZZ_SECONDS) tests/fuzz/corpus/$$t || exit 1; \
	done
else
fuzz fuzz-run:
	$(MAKE) MODE=fuzz $@
endif

clean:
	rm -rf build

-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(BUILD)/src/relay/main.d $(BUILD)/tools/hookdump.d \
    $(wildcard $(BUILD)/tests/fuzz/*.d)
