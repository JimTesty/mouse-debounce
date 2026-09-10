#ifndef MOUSE_DEBOUNCE_EVENT_LOG_H
#define MOUSE_DEBOUNCE_EVENT_LOG_H

#include "debounce_logic.h"

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *file;
    uint64_t last_event_ns;
    bool has_event;
} EventLog;

/* Append to events.log beside the selected config file. */
bool event_log_open(EventLog *log, const char *config_path);
bool event_log_handle(EventLog *log, CGEventType type, CGEventRef event, DebounceAction action);
void event_log_close(EventLog *log);

#endif
