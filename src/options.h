#ifndef MOUSE_DEBOUNCE_OPTIONS_H
#define MOUSE_DEBOUNCE_OPTIONS_H

#include "mouse_events.h"

#include <stdbool.h>

typedef enum {
    APP_MODE_FILTER = 0,
    APP_MODE_MEASURE,
} AppMode;

typedef struct {
    AppMode mode;
    double short_ms;
    double hold_ms;
    bool buttons[MOUSE_BUTTON_COUNT];
    const char *output_path;
    double duration_seconds;
} AppOptions;

bool options_parse(int argc, char **argv, AppOptions *options);
void options_print_usage(const char *argv0);

#endif
