#ifndef MOUSE_DEBOUNCE_MEASUREMENT_H
#define MOUSE_DEBOUNCE_MEASUREMENT_H

#include "mouse_events.h"

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MEASUREMENT_MAX_SAMPLES 4096

typedef struct {
    bool enabled[MOUSE_BUTTON_COUNT];
    FILE *out;
    uint64_t first_ns;
    uint64_t last_down_ns[MOUSE_BUTTON_COUNT];
    uint64_t last_up_ns[MOUSE_BUTTON_COUNT];
    double press_ms[MOUSE_BUTTON_COUNT][MEASUREMENT_MAX_SAMPLES];
    double gap_ms[MOUSE_BUTTON_COUNT][MEASUREMENT_MAX_SAMPLES];
    size_t press_count[MOUSE_BUTTON_COUNT];
    size_t gap_count[MOUSE_BUTTON_COUNT];
    uint64_t last_scroll_ns;
    size_t scroll_count;
} Measurement;

void measurement_init(
    Measurement *measurement,
    const bool enabled[MOUSE_BUTTON_COUNT],
    FILE *out
);
void measurement_handle(Measurement *measurement, CGEventType type, CGEventRef event);
void measurement_print_summary(Measurement *measurement);

#endif
