#ifndef MOUSE_DEBOUNCE_FILTER_H
#define MOUSE_DEBOUNCE_FILTER_H

#include "debounce_logic.h"
#include "mouse_events.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    DebounceState logic;
    CGEventRef pending_up;
    CFRunLoopTimerRef pending_timer;
} DebounceButtonRuntime;

typedef struct {
    bool enabled[MOUSE_BUTTON_COUNT];
    uint64_t short_ns;
    uint64_t hold_ns;
    DebounceButtonRuntime button[MOUSE_BUTTON_COUNT];
} DebounceFilter;

void debounce_filter_init(
    DebounceFilter *filter,
    const bool enabled[MOUSE_BUTTON_COUNT],
    double short_ms,
    double hold_ms
);
CGEventRef debounce_filter_handle(
    DebounceFilter *filter,
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event
);
void debounce_filter_flush(DebounceFilter *filter);
void debounce_filter_reset_safely(DebounceFilter *filter);
void debounce_filter_destroy(DebounceFilter *filter);

#endif
