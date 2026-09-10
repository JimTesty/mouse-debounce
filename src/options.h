#ifndef MOUSE_DEBOUNCE_OPTIONS_H
#define MOUSE_DEBOUNCE_OPTIONS_H

#include "config_file.h"
#include "mouse_button.h"
#include "timing_settings.h"

#include <stdbool.h>

typedef enum {
    APP_MODE_FILTER = 0,
    APP_MODE_MEASURE,
} AppMode;

typedef struct {
    AppMode mode;
    bool debug;
    bool debug_wheel;
    bool log;
    TimingDraft timing_draft;
    TimingSettings timing;
    bool buttons[MOUSE_BUTTON_COUNT];
    double sound_volume;
    const char *output_path;
    double duration_seconds;
    bool use_config;
    bool save_config_and_exit;
    const char *pid_file;
    char config_path[CONFIG_PATH_SIZE];
} AppOptions;

bool options_parse(int argc, char **argv, AppOptions *options);
void options_print_usage(const char *argv0);

#endif
