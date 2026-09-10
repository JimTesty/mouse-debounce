#ifndef MOUSE_DEBOUNCE_CONFIG_FILE_H
#define MOUSE_DEBOUNCE_CONFIG_FILE_H

#include "timing_settings.h"

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_MAX_TOKENS 128
#define CONFIG_TOKEN_SIZE 128
#define CONFIG_PATH_SIZE 1024

typedef struct {
    int count;
    char storage[CONFIG_MAX_TOKENS][CONFIG_TOKEN_SIZE];
    char *tokens[CONFIG_MAX_TOKENS];
} ConfigTokens;

bool config_default_path(char path[CONFIG_PATH_SIZE]);
bool config_load_tokens(const char *path, ConfigTokens *tokens);
bool config_write_settings(
    const char *path,
    const TimingSettings *timing,
    const bool buttons[MOUSE_BUTTON_COUNT],
    double sound_volume,
    bool debug
);

#endif
