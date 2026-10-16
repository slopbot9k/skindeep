# The C tree of decision 0036. It needs GNU make 3.81 or later, which
# macOS ships as /usr/bin/make and Linux distributions ship as make, and
# the system C compiler.
#
#   make              build the library, the tests, skinned, skinned-relay,
#                     skinc and hookdump
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
# What a program with windows links besides (platform/ui.h): AppKit and
# CoreGraphics through the Objective-C runtime on macOS; nothing on
# Linux, where X11 is a socket. The relays link none of it.
ifeq ($(OS),Darwin)
UI_LIBS := -framework AppKit -framework QuartzCore -framework CoreGraphics -framework CoreFoundation -lobjc
endif
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

# X11's protocol code is plain C over a socket: it builds, and its tests
# run against a fake server, on every system; only Linux opens windows
# with it.
LIB_SRC := $(wildcard src/base/*.c src/core/*.c src/agents/*.c src/agents/*/*.c src/app/*.c src/sessions/*.c \
    src/gfx/*.c src/text/*.c src/audio/*.c src/term/*.c src/ui/*.c src/vendor/*.c src/platform/x11/*.c src/platform/$(PLATFORM)/*.c) src/relay/relay.c
# The embedded DejaVu, in sk_embedded_font's order (text/text.h).
FONTS := $(addprefix third_party/dejavu/,DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf DejaVuSansMono-Oblique.ttf \
    DejaVuSansMono-BoldOblique.ttf DejaVuSans.ttf DejaVuSans-Bold.ttf)
GEN_OBJ := $(BUILD)/gen/fonts.o
LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o) $(GEN_OBJ)
BIN2C := $(BUILD)/bin2c
LIB := $(BUILD)/libsk.a

# skinc (decision 0037) builds from skinc/, its front end a library of
# its own that the tests and the fuzz targets link before the app's.
SKINC_SRC := $(filter-out skinc/main.c,$(wildcard skinc/*.c))
SKINC_OBJ := $(SKINC_SRC:skinc/%.c=$(BUILD)/skinc-objs/%.o)
SKINC_LIB := $(BUILD)/libskinc.a
SKINC := $(BUILD)/skinc

TEST_SRC := $(filter-out tests/fuzz/%,$(wildcard tests/*.c tests/*/*.c))
TEST_OBJ := $(TEST_SRC:%.c=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/tests/run

RELAY := $(BUILD)/skinned-relay
HOOKDUMP := $(BUILD)/hookdump
SKINNED := $(BUILD)/skinned

FUZZ_TARGETS := json http utf8 base64 claude codex text path links term css
FUZZ_BINS := $(FUZZ_TARGETS:%=$(BUILD)/fuzz-%)
FUZZ_DRIVER := $(BUILD)/tests/fuzz/driver.o
FUZZ_SECONDS ?= 30

.PHONY: all test debug fuzz fuzz-run clean macdemo windemo
.SECONDARY:

all: $(LIB) $(TEST_BIN) $(RELAY) $(HOOKDUMP) $(SKINNED) $(SKINC) $(BUILD)/qoi2png $(BUILD)/windemo

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE) -c $< -o $@

# skinc's objects live apart from the program of that name.
$(BUILD)/skinc-objs/%.o: skinc/%.c
	@mkdir -p $(dir $@)
	$(COMPILE) -c $< -o $@

# Single-file libraries of third_party/ compile in src/vendor/, one unit
# each, with the warnings their authors' code meets.
$(BUILD)/src/vendor/%.o: WARN := -w

# Generated sources: tools/bin2c runs on the build machine.
$(BIN2C): tools/bin2c.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -o $@ $<

$(BUILD)/gen/fonts.c: $(FONTS) $(BIN2C)
	@mkdir -p $(dir $@)
	$(BIN2C) sk_embedded_fonts $(FONTS) > $@.tmp && mv $@.tmp $@

$(BUILD)/gen/%.o: $(BUILD)/gen/%.c
	$(COMPILE) -I. -c $< -o $@

# The driver holds the coverage callbacks, so it is not instrumented.
$(FUZZ_DRIVER): COVERAGE :=

$(LIB): $(LIB_OBJ)
	@rm -f $@
	$(AR) rcs $@ $(LIB_OBJ)

$(SKINC_LIB): $(SKINC_OBJ)
	@rm -f $@
	$(AR) rcs $@ $(SKINC_OBJ)

$(TEST_BIN): $(TEST_OBJ) $(SKINC_LIB) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $(TEST_OBJ) $(SKINC_LIB) $(LIB) $(LDLIBS) $(UI_LIBS)

$(SKINC): $(BUILD)/skinc-objs/main.o $(SKINC_LIB) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(RELAY): $(BUILD)/src/relay/main.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SKINNED): $(BUILD)/src/main/skinned.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

$(HOOKDUMP): $(BUILD)/tools/hookdump.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# A golden image or pack image as a PNG to look at.
$(BUILD)/qoi2png: $(BUILD)/tools/qoi2png.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# One window of platform/ui.h to look at by hand; not part of all.
macdemo: $(BUILD)/macdemo
$(BUILD)/macdemo: $(BUILD)/tools/macdemo.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

# Phase 3's window, drawing and text together, on either system.
windemo: $(BUILD)/windemo
$(BUILD)/windemo: $(BUILD)/tools/windemo.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

# Times a full frame composited at Retina 2x: make build/release/gfxbench.
$(BUILD)/gfxbench: $(BUILD)/tools/gfxbench.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(TEST_BIN) $(RELAY) $(SKINNED) $(SKINC)
	SK_RELAY_BIN=$(RELAY) SK_SKINNED_BIN=$(SKINNED) SK_SKINC_BIN=$(SKINC) ./$(TEST_BIN) $(T)

debug:
	$(MAKE) MODE=debug test

$(BUILD)/fuzz-%: $(BUILD)/tests/fuzz/%.o $(FUZZ_DRIVER) $(SKINC_LIB) $(LIB)
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

-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(SKINC_OBJ:.o=.d) $(BUILD)/skinc-objs/main.d $(BUILD)/src/relay/main.d $(BUILD)/tools/hookdump.d \
    $(BUILD)/src/main/skinned.d \
    $(BUILD)/tools/qoi2png.d $(BUILD)/tools/macdemo.d $(BUILD)/tools/windemo.d $(BUILD)/tools/gfxbench.d \
    $(wildcard $(BUILD)/tests/fuzz/*.d)
