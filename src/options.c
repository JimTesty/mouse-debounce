#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SHORT_MS 80.0
#define DEFAULT_HOLD_MS 70.0

void options_print_usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [--filter] [--short-ms N] [--hold-ms N] [--buttons LIST]\n"
        "  %s --measure [--buttons LIST] [--output PATH] [--duration SEC]\n"
        "\n"
        "Defaults:\n"
        "  mode: filter\n"
        "  buttons: left,right,middle\n"
        "  short-ms: %.0f\n"
        "  hold-ms: %.0f\n"
        "\n"
        "Measurement also records scroll-wheel events, but never modifies them.\n",
        argv0, argv0, DEFAULT_SHORT_MS, DEFAULT_HOLD_MS);
}

bool options_parse(int argc, char **argv, AppOptions *options) {
    options->mode = APP_MODE_FILTER;
    options->short_ms = DEFAULT_SHORT_MS;
    options->hold_ms = DEFAULT_HOLD_MS;
    options->output_path = NULL;
    options->duration_seconds = 0.0;
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) options->buttons[i] = true;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--filter") == 0) {
            options->mode = APP_MODE_FILTER;
        } else if (strcmp(argv[i], "--measure") == 0) {
            options->mode = APP_MODE_MEASURE;
        } else if (strcmp(argv[i], "--short-ms") == 0 && i + 1 < argc) {
            options->short_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--hold-ms") == 0 && i + 1 < argc) {
            options->hold_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--buttons") == 0 && i + 1 < argc) {
            if (!mouse_parse_button_list(argv[++i], options->buttons)) return false;
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            options->output_path = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            options->duration_seconds = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            options_print_usage(argv[0]);
            exit(0);
        } else {
            return false;
        }
    }

    return options->short_ms > 0.0 && options->hold_ms > 0.0 && options->duration_seconds >= 0.0;
}
