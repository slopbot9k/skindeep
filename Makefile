# The C tree of decision 0036. It needs GNU make 3.81 or later, which
# macOS ships as /usr/bin/make and Linux distributions ship as make, and
# the system C compiler.
#
#   make              build the library, the tests, skinned, skinned-relay,
#                     skinc, hookdump, the fake claude and the shipped
#                     skins' packs
#   make programs     build skinned, skinned-relay and skinc alone
#   make install      build them with WERROR empty, and link skinned,
#                     claude-skins and codex-skins into BINDIR (default
#                     ~/.local/bin), decision 0018
#   make uninstall    remove those links from BINDIR
#   make test         run the tests (T=json runs the json suite)
#   make debug        run the tests under AddressSanitizer and UBSan
#   make fuzz         build the fuzz targets
#   make fuzz-run     run each target for FUZZ_SECONDS (default 30)
#   make milkshot     build the MilkDrop renderer's frame tool
#   make vizwin       build the visualization's window on fake tabs
#   make clean
#
# MODE picks the build: release (the default), debug or fuzz, each in
# build/<mode>/.

OS := $(shell uname -s)
MODE ?= release
BUILD := build/$(MODE)

CSTD := -std=c17
# make install builds with WERROR empty: a warning a newer compiler adds must
# not stop an install. The tests and CI keep -Werror.
WERROR ?= -Werror
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
        -Wmissing-prototypes -Wvla -Wformat=2 -Wundef -Wpointer-arith $(WERROR)
DEFS := -D_XOPEN_SOURCE=700
# Apple's headers hide parts of POSIX.1-2008, such as mkdtemp, unless
# this is set. Linux keeps the strict view, so CI there still catches
# anything outside POSIX.
ifeq ($(OS),Darwin)
DEFS += -D_DARWIN_C_SOURCE
endif
LDLIBS := -lpthread -lm
# What a program with windows links besides (platform/ui.h): AppKit and
# CoreGraphics through the Objective-C runtime on macOS, and AudioToolbox
# for the sound output (platform/audio.h); nothing on Linux, where X11
# is a socket and sound a player child. The relays link none of it.
ifeq ($(OS),Darwin)
UI_LIBS := -framework AppKit -framework QuartzCore -framework CoreGraphics -framework CoreFoundation \
    -framework AudioToolbox -lobjc
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
    src/gfx/*.c src/text/*.c src/skin/*.c src/term/*.c src/ui/*.c src/audio/*.c src/viz/*.c src/vendor/*.c \
    src/platform/x11/*.c src/platform/$(PLATFORM)/*.c) src/relay/relay.c
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

# The shipped skins' packs (decision 0037), compiled by this build's
# skinc and linked into skinned and the tests as generated C arrays;
# not into libsk.a, which skinc links. A pack depends on every file of
# its skin.
SKINS := coffee deck library phosphor
PACKS := $(SKINS:%=$(BUILD)/gen/%.pack)
SHIPPED_OBJ := $(BUILD)/gen/packs.o
rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d)) $(d))

TEST_SRC := $(filter-out tests/fuzz/%,$(wildcard tests/*.c tests/*/*.c))
TEST_OBJ := $(TEST_SRC:%.c=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/tests/run

# A shipped skin drawn whole (tools/shot.c), for skinshot and the skin
# goldens, its images decoded by skinc's decoders.
SHOT_OBJ := $(BUILD)/tools/shot.o

RELAY := $(BUILD)/skinned-relay
HOOKDUMP := $(BUILD)/hookdump
# The replay fake named claude (tools/fakeclaude.c), in a folder of its own
# so that PATH can put it first.
FAKE := $(BUILD)/fake/claude
SKINNED := $(BUILD)/skinned

FUZZ_TARGETS := json http utf8 base64 claude codex text path links term css png jpeg svg vorbis fontcheck adpcm \
    pack
FUZZ_BINS := $(FUZZ_TARGETS:%=$(BUILD)/fuzz-%)
FUZZ_DRIVER := $(BUILD)/tests/fuzz/driver.o
FUZZ_SECONDS ?= 30

.PHONY: all programs install uninstall test debug fuzz fuzz-run clean macdemo windemo milkshot skinshot skinrun vizshot vizwin
.SECONDARY:

all: $(LIB) $(TEST_BIN) $(RELAY) $(HOOKDUMP) $(SKINNED) $(SKINC) $(FAKE) $(BUILD)/qoi2png $(BUILD)/windemo

programs: $(RELAY) $(SKINNED) $(SKINC)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE) -c $< -o $@

# skinc's objects live apart from the program of that name.
$(BUILD)/skinc-objs/%.o: skinc/%.c
	@mkdir -p $(dir $@)
	$(COMPILE) -c $< -o $@

# Single-file libraries of third_party/ compile in src/vendor/, one unit
# each, with the warnings their authors' code meets; the decoders only
# skinc runs, in skinc/vendor_*.c, the same.
$(BUILD)/src/vendor/%.o: WARN := -w
$(BUILD)/skinc-objs/vendor_%.o: WARN := -w
# Hostile input makes the decoders' own arithmetic overflow where no
# check before the call can rule it out: stb_image's IDCT on huge
# coefficients, nanosvg's fixed point of a near-flat edge's slope. It
# wraps to wrong pixels; the sanitizers still watch the rest, their
# memory above all.
$(BUILD)/skinc-objs/vendor_%.o: OPT += -fno-sanitize=signed-integer-overflow,float-cast-overflow

# The MilkDrop renderer feeds each frame into the next: no fused
# multiply-add, so gcc and clang round its arithmetic alike. A pattern's
# stem is never empty, so milk.o is named apart.
$(BUILD)/src/viz/milk.o $(BUILD)/src/viz/milk%.o: OPT += -ffp-contract=off

# Generated sources: tools/bin2c runs on the build machine.
$(BIN2C): tools/bin2c.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -o $@ $<

$(BUILD)/gen/fonts.c: $(FONTS) $(BIN2C)
	@mkdir -p $(dir $@)
	$(BIN2C) sk_embedded_fonts $(FONTS) > $@.tmp && mv $@.tmp $@

define pack_rule
$(BUILD)/gen/$(1).pack: $(call rwildcard,skins/$(1)) $(SKINC)
	@mkdir -p $$(dir $$@)
	$(SKINC) skins/$(1) -o $$@
endef
$(foreach s,$(SKINS),$(eval $(call pack_rule,$(s))))

$(BUILD)/gen/packs.c: $(PACKS) $(BIN2C)
	@mkdir -p $(dir $@)
	$(BIN2C) sk_shipped_packs $(PACKS) > $@.tmp && mv $@.tmp $@

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

$(TEST_BIN): $(TEST_OBJ) $(SHOT_OBJ) $(SHIPPED_OBJ) $(SKINC_LIB) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $(TEST_OBJ) $(SHOT_OBJ) $(SHIPPED_OBJ) $(SKINC_LIB) $(LIB) $(LDLIBS) $(UI_LIBS)

$(SKINC): $(BUILD)/skinc-objs/main.o $(SKINC_LIB) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(RELAY): $(BUILD)/src/relay/main.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SKINNED): $(BUILD)/src/main/skinned.o $(SHIPPED_OBJ) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

$(HOOKDUMP): $(BUILD)/tools/hookdump.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# The fake reads the checkout's fixtures, wherever it is run from.
$(BUILD)/tools/fakeclaude.o: DEFS += -DSK_FAKE_FIXTURES='"$(CURDIR)/tests/fixtures/claude"'
$(FAKE): $(BUILD)/tools/fakeclaude.o $(LIB)
	@mkdir -p $(dir $@)
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

# A MilkDrop preset run on the test signal, its frames as QOI and its
# frame times: make milkshot, then build/release/milkshot.
milkshot: $(BUILD)/milkshot
$(BUILD)/milkshot: $(BUILD)/tools/milkshot.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# A shipped skin laid out and painted at 1x and 2x, to look at beside the
# Electron app's screenshots; not part of all.
skinshot: $(BUILD)/skinshot
$(BUILD)/skinshot: $(BUILD)/tools/skinshot.o $(SHOT_OBJ) $(SKINC_LIB) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# A shipped skin live in a window on the scripted session, to hover and
# click, read back with -o, or its frames timed with -b: make skinrun.
# Not part of all.
skinrun: $(BUILD)/skinrun
$(BUILD)/skinrun: $(BUILD)/tools/skinrun.o $(SHIPPED_OBJ) $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

# A visualizer mode run on the scripted session, its last frame as QOI,
# or timed with -b: make vizshot. Not part of all.
vizshot: $(BUILD)/vizshot
$(BUILD)/vizshot: $(BUILD)/tools/vizshot.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# The visualization's window on fake tabs fed by the scripted session,
# to look at, or drawn offscreen and timed with -b: make vizwin. Not
# part of all.
vizwin: $(BUILD)/vizwin
$(BUILD)/vizwin: $(BUILD)/tools/vizwin.o $(LIB)
	$(CC) $(OPT) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(UI_LIBS)

test: $(TEST_BIN) $(RELAY) $(SKINNED) $(SKINC) $(FAKE)
	SK_RELAY_BIN=$(RELAY) SK_SKINNED_BIN=$(SKINNED) SK_SKINC_BIN=$(SKINC) SK_FAKE_DIR=$(dir $(FAKE)) ./$(TEST_BIN) $(T)

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


# Decision 0018: skinned, claude-skins and codex-skins in BINDIR, each a
# link to this checkout's release skinned, which acts as each by its
# name and finds skinned-relay and skins/ from where it lies. A file
# there that is no such link is left alone and named; a wrapper an
# earlier install.sh wrote, its marker on its second line, is replaced.
# A moved checkout is mended by installing again.
BINDIR ?= $(HOME)/.local/bin
INSTALLED := $(abspath build/release/skinned)
COMMANDS := skinned claude-skins codex-skins
MARKER := \# generated by claude-skins install.sh
# What some parts use when it is there, one of each group enough: the
# issue boards, and on Linux the folder picker, sounds and speaker
# capture.
ifeq ($(OS),Darwin)
OPTIONAL := gh
else
OPTIONAL := gh curl zenity,kdialog pw-play,paplay,aplay pw-record,parec
endif

install:
	@command -v $(CC) >/dev/null 2>&1 || { echo "install: no C compiler ($(CC)) on PATH" >&2; exit 1; }
	$(MAKE) MODE=release WERROR= programs
	@mkdir -p "$(BINDIR)"
	@for c in $(COMMANDS); do \
	    t="$(BINDIR)/$$c"; \
	    if [ -L "$$t" ] && [ "$$(readlink "$$t")" = "$(INSTALLED)" ]; then echo "install: $$t"; continue; fi; \
	    if [ -L "$$t" ] || [ -e "$$t" ]; then \
	        if [ -f "$$t" ] && [ ! -L "$$t" ] && [ "$$(sed -n '2{p;q;}' "$$t")" = "$(MARKER)" ]; then rm -f "$$t"; \
	        else echo "install: $$t exists and was not made by this install, left alone" >&2; continue; fi; \
	    fi; \
	    ln -s "$(INSTALLED)" "$$t" && echo "install: $$t"; \
	 done
	@case ":$$PATH:" in *":$(BINDIR):"*) ;; *) echo "install: $(BINDIR) is not on PATH, add it to run the commands by name" >&2 ;; esac
	@for g in $(OPTIONAL); do \
	    found=; for t in $$(echo $$g | tr , ' '); do command -v $$t >/dev/null 2>&1 && found=1; done; \
	    [ -n "$$found" ] || missing="$$missing $$(echo $$g | sed 's/,/ or /g')"; \
	 done; \
	 [ -z "$$missing" ] || echo "install: optional, not found:$$missing (decision 0018 says what uses each)" >&2

uninstall:
	@for t in "$(BINDIR)"/*; do \
	    if [ -L "$$t" ] && [ "$$(readlink "$$t")" = "$(INSTALLED)" ]; then rm -f "$$t" && echo "uninstall: removed $$t"; \
	    elif [ -f "$$t" ] && [ ! -L "$$t" ] && [ "$$(sed -n '2{p;q;}' "$$t")" = "$(MARKER)" ]; then rm -f "$$t" && echo "uninstall: removed $$t"; fi; \
	 done

clean:
	rm -rf build

-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(SKINC_OBJ:.o=.d) $(BUILD)/skinc-objs/main.d $(BUILD)/src/relay/main.d $(BUILD)/tools/hookdump.d \
    $(BUILD)/src/main/skinned.d \
    $(BUILD)/tools/fakeclaude.d \
    $(BUILD)/tools/qoi2png.d $(BUILD)/tools/macdemo.d $(BUILD)/tools/windemo.d $(BUILD)/tools/gfxbench.d $(BUILD)/tools/vizshot.d \
    $(BUILD)/tools/vizwin.d $(BUILD)/tools/skinrun.d \
    $(wildcard $(BUILD)/tests/fuzz/*.d)
