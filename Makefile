CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
DEPFLAGS := -MMD -MP
MACOSX_DEPLOYMENT_TARGET ?= 10.15
SIGN_IDENTITY ?= -
export MACOSX_DEPLOYMENT_TARGET

EXECUTABLE := MouseDebounce
BUNDLE_ID := io.mouse-debounce.MouseDebounce
BUILD := build
OBJ := $(BUILD)/obj
APP := $(BUILD)/MouseDebounce.app
MACOS := $(APP)/Contents/MacOS
BINARY := $(MACOS)/$(EXECUTABLE)

SOURCES := \
	src/main.c \
	src/options.c \
	src/config_file.c \
	src/mouse_button.c \
	src/mouse_events.c \
	src/timing_settings.c \
	src/statistics.c \
	src/wheel_analysis.c \
	src/monotonic_clock.c \
	src/debounce_logic.c \
	src/debounce_filter.c \
	src/measurement.c \
	src/event_tap.c \
	src/permissions.c \
	src/signal_bridge.c
OBJECTS := $(patsubst src/%.c,$(OBJ)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
FRAMEWORKS := -framework ApplicationServices -framework CoreFoundation

.PHONY: all app test clean run install-user

all: app test

app: $(BINARY)
	codesign --force --sign "$(SIGN_IDENTITY)" --identifier "$(BUNDLE_ID)" "$(APP)"

$(BINARY): $(OBJECTS) resources/Info.plist
	mkdir -p "$(MACOS)"
	cp resources/Info.plist "$(APP)/Contents/Info.plist"
	$(CC) $(CFLAGS) $(OBJECTS) -o "$@" $(FRAMEWORKS)

$(OBJ)/%.o: src/%.c
	mkdir -p "$(OBJ)"
	$(CC) $(CFLAGS) $(DEPFLAGS) -Isrc -c $< -o $@

test: $(BUILD)/test-debounce-logic $(BUILD)/test-timing-settings $(BUILD)/test-statistics $(BUILD)/test-wheel-analysis $(BUILD)/test-options
	$(BUILD)/test-debounce-logic
	$(BUILD)/test-timing-settings
	$(BUILD)/test-statistics
	$(BUILD)/test-wheel-analysis
	$(BUILD)/test-options

$(BUILD)/test-options: tests/test_options.c src/options.c src/options.h src/config_file.c src/config_file.h src/mouse_button.c src/mouse_button.h src/timing_settings.c src/timing_settings.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_options.c src/options.c src/config_file.c src/mouse_button.c src/timing_settings.c -o $@

$(BUILD)/test-debounce-logic: tests/test_debounce_logic.c src/debounce_logic.c src/debounce_logic.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_debounce_logic.c src/debounce_logic.c -o $@

$(BUILD)/test-timing-settings: tests/test_timing_settings.c src/timing_settings.c src/timing_settings.h src/mouse_button.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_timing_settings.c src/timing_settings.c -o $@

$(BUILD)/test-statistics: tests/test_statistics.c src/statistics.c src/statistics.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_statistics.c src/statistics.c -o $@

$(BUILD)/test-wheel-analysis: tests/test_wheel_analysis.c src/wheel_analysis.c src/wheel_analysis.h
	mkdir -p "$(BUILD)"
	$(CC) $(CFLAGS) -Isrc tests/test_wheel_analysis.c src/wheel_analysis.c -lm -o $@

run: app
	open -n "$(APP)"

install-user: app
	tools/mousedebouncectl install

clean:
	rm -rf "$(BUILD)"

-include $(DEPS)
