CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
MACOSX_DEPLOYMENT_TARGET ?= 10.15
export MACOSX_DEPLOYMENT_TARGET

EXECUTABLE := MouseDebounce
BUNDLE_ID := io.mouse-debounce.MouseDebounce
BUILD := build
OBJ := $(BUILD)/obj
APP := $(BUILD)/MouseDebounce.app
MACOS := $(APP)/Contents/MacOS

SOURCES := \
	src/main.c \
	src/options.c \
	src/mouse_events.c \
	src/debounce_logic.c \
	src/debounce_filter.c \
	src/measurement.c \
	src/event_tap.c \
	src/permissions.c \
	src/signal_bridge.c
OBJECTS := $(patsubst src/%.c,$(OBJ)/%.o,$(SOURCES))
FRAMEWORKS := -framework ApplicationServices -framework CoreFoundation

.PHONY: all app test clean run

all: app test

app: $(APP)

$(APP): $(OBJECTS) resources/Info.plist
	mkdir -p "$(MACOS)"
	cp resources/Info.plist "$(APP)/Contents/Info.plist"
	$(CC) $(CFLAGS) $(OBJECTS) -o "$(MACOS)/$(EXECUTABLE)" $(FRAMEWORKS)
	codesign --force --sign - --identifier "$(BUNDLE_ID)" "$(APP)"

$(OBJ)/%.o: src/%.c
	mkdir -p "$(OBJ)"
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

test: $(BUILD)/test-debounce-logic
	$(BUILD)/test-debounce-logic

$(BUILD)/test-debounce-logic: tests/test_debounce_logic.c src/debounce_logic.c src/debounce_logic.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_debounce_logic.c src/debounce_logic.c -o $@

run: app
	open -n "$(APP)"

clean:
	rm -rf "$(BUILD)"
