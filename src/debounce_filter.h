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
    bool debug;
    uint64_t short0_ns[MOUSE_BUTTON_COUNT];
    uint64_t hold0_ns[MOUSE_BUTTON_COUNT];
    uint64_t hold_ns[MOUSE_BUTTON_COUNT];
    DebounceButtonRuntime button[MOUSE_BUTTON_COUNT];
} DebounceFilter;

/* Keep the filter at a stable address until destroy/abandon cancels its timers. */
void debounce_filter_init(
    DebounceFilter *filter,
    const bool enabled[MOUSE_BUTTON_COUNT],
    const double short0_ms[MOUSE_BUTTON_COUNT],
    const double hold0_ms[MOUSE_BUTTON_COUNT],
    const double hold_ms[MOUSE_BUTTON_COUNT]
);
bool debounce_filter_is_owned_event(CGEventRef event);

/* Reports the decision for logging without running bounce detection again. */
CGEventRef debounce_filter_handle(
    DebounceFilter *filter,
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event,
    DebounceAction *action_out
);
void debounce_filter_flush(DebounceFilter *filter);
void debounce_filter_reset_safely(DebounceFilter *filter);
void debounce_filter_abandon(DebounceFilter *filter);
void debounce_filter_destroy(DebounceFilter *filter);

#endif
