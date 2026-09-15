#ifndef MOUSE_DEBOUNCE_EVENT_LOG_H
#define MOUSE_DEBOUNCE_EVENT_LOG_H

#include "debounce_logic.h"
#include "timing_settings.h"

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define EVENT_LOG_BUTTON_SLOTS 11 /* Button IDs 0-9 plus the OTHERX bucket. */

typedef struct {
    bool has_event;
    bool has_down;
    bool pending_uses_hold0;
    uint64_t last_event_ns;
    uint64_t last_down_ns;
} EventLogButton;

typedef struct {
    FILE *file;
    uint64_t last_event_ns;
    bool has_event;
    EventLogButton buttons[EVENT_LOG_BUTTON_SLOTS];
} EventLog;

/* Append to events.log beside the selected config file. */
bool event_log_open(EventLog *log, const char *config_path);
bool event_log_handle(
    EventLog *log,
    CGEventType type,
    CGEventRef event,
    DebounceAction action,
    const TimingSettings *timing
);
void event_log_close(EventLog *log);

#endif
