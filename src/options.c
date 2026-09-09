#include "options.h"

#include "mouse_button.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Half the nanosecond range leaves headroom for rounding and deadline addition. */
static const double kMaxTimingMs = (double)(UINT64_MAX / 2) / 1000000.0;

static bool parse_positive_double(const char *text, double *out) {
    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value) ||
        value <= 0.0 || value > kMaxTimingMs) return false;
    *out = value;
    return true;
}

static bool parse_nonnegative_double(const char *text, double *out) {
    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value) || value < 0.0) return false;
    *out = value;
    return true;
}

static bool parse_unit_double(const char *text, double *out) {
    if (!parse_nonnegative_double(text, out)) return false;
    return *out <= 1.0;
}

void options_print_usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [--filter] [timing options] [--buttons LIST] [--sound-volume N]\n"
        "  %s --measure [--buttons LIST] [--output PATH] [--duration SEC]\n"
        "\n"
        "Timing options:\n"
        "  --short-ms N / --hold-ms N             set all buttons\n"
        "  --left-short-ms N / --left-hold-ms N\n"
        "  --right-short-ms N / --right-hold-ms N\n"
        "  --middle-short-ms N / --middle-hold-ms N\n"
        "\n"
        "Sound:\n"
        "  --sound-volume N   debug tick volume, 0..1 (default 0.1)\n"
        "\n"
        "Unset per-button values inherit the average of explicitly set siblings;\n"
        "if no sibling is set, the default is %.0f/%.0f ms. Later arguments win.\n"
        "\n"
        "Config:\n"
        "  --config PATH      use a different config.args file\n"
        "  --no-config        ignore the default config file\n"
        "  --save-config-and-exit  save resolved filter settings and exit\n"
        "\n"
        "Default config: ~/Library/Application Support/MouseDebounce/config.args\n"
        "Default buttons: left,right,middle\n"
        "Measurement diagnoses wheel timing but never modifies wheel events.\n",
        argv0, argv0, DEFAULT_SHORT_MS, DEFAULT_HOLD_MS);
}

static bool parse_button_timing(
    const char *name,
    const char *value_text,
    TimingDraft *draft
) {
    double value;
    if (!parse_positive_double(value_text, &value)) return false;

    if (strcmp(name, "--short-ms") == 0) timing_set_short_all(draft, value);
    else if (strcmp(name, "--hold-ms") == 0) timing_set_hold_all(draft, value);
    else if (strcmp(name, "--left-short-ms") == 0) timing_set_short_button(draft, MOUSE_BUTTON_LEFT, value);
    else if (strcmp(name, "--right-short-ms") == 0) timing_set_short_button(draft, MOUSE_BUTTON_RIGHT, value);
    else if (strcmp(name, "--middle-short-ms") == 0) timing_set_short_button(draft, MOUSE_BUTTON_MIDDLE, value);
    else if (strcmp(name, "--left-hold-ms") == 0) timing_set_hold_button(draft, MOUSE_BUTTON_LEFT, value);
    else if (strcmp(name, "--right-hold-ms") == 0) timing_set_hold_button(draft, MOUSE_BUTTON_RIGHT, value);
    else if (strcmp(name, "--middle-hold-ms") == 0) timing_set_hold_button(draft, MOUSE_BUTTON_MIDDLE, value);
    else return false;
    return true;
}

static bool is_timing_option(const char *name) {
    return strcmp(name, "--short-ms") == 0 || strcmp(name, "--hold-ms") == 0 ||
        strcmp(name, "--left-short-ms") == 0 || strcmp(name, "--right-short-ms") == 0 ||
        strcmp(name, "--middle-short-ms") == 0 || strcmp(name, "--left-hold-ms") == 0 ||
        strcmp(name, "--right-hold-ms") == 0 || strcmp(name, "--middle-hold-ms") == 0;
}

static bool parse_sequence(
    int argc,
    char **argv,
    AppOptions *options,
    bool config_mode
) {
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (is_timing_option(arg)) {
            if (i + 1 >= argc || !parse_button_timing(arg, argv[++i], &options->timing_draft)) return false;
        } else if (strcmp(arg, "--buttons") == 0) {
            if (i + 1 >= argc || !mouse_parse_button_list(argv[++i], options->buttons)) return false;
        } else if (strcmp(arg, "--sound-volume") == 0) {
            if (i + 1 >= argc || !parse_unit_double(argv[++i], &options->sound_volume)) return false;
        } else if (config_mode) {
            return false;
        } else if (strcmp(arg, "--filter") == 0) {
            options->mode = APP_MODE_FILTER;
        } else if (strcmp(arg, "--measure") == 0) {
            options->mode = APP_MODE_MEASURE;
        } else if (strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) return false;
            options->output_path = argv[++i];
        } else if (strcmp(arg, "--duration") == 0) {
            if (i + 1 >= argc || !parse_nonnegative_double(argv[++i], &options->duration_seconds)) return false;
        } else if (strcmp(arg, "--save-config-and-exit") == 0) {
            options->save_config_and_exit = true;
        } else if (strcmp(arg, "--pid-file") == 0) {
            if (i + 1 >= argc) return false;
            options->pid_file = argv[++i];
        } else if (strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) return false;
            ++i;
        } else if (strcmp(arg, "--no-config") == 0) {
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            options_print_usage("MouseDebounce");
            exit(0);
        } else {
            return false;
        }
    }
    return true;
}

static bool pre_scan_config(int argc, char **argv, AppOptions *options) {
    options->use_config = true;
    if (!config_default_path(options->config_path)) return false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-config") == 0) {
            options->use_config = false;
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) return false;
            size_t len = strlen(argv[++i]);
            if (len >= sizeof(options->config_path)) return false;
            strcpy(options->config_path, argv[i]);
            options->use_config = true;
        }
    }
    return true;
}

bool options_parse(int argc, char **argv, AppOptions *options) {
    memset(options, 0, sizeof(*options));
    options->mode = APP_MODE_FILTER;
    options->duration_seconds = 0.0;
    options->sound_volume = 0.1;
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) options->buttons[i] = true;
    timing_draft_init(&options->timing_draft);

    if (!pre_scan_config(argc, argv, options)) return false;

    if (options->use_config) {
        ConfigTokens config;
        if (!config_load_tokens(options->config_path, &config)) {
            fprintf(stderr, "Could not read config file: %s\n", options->config_path);
            return false;
        }
        if (config.count > 0 && !parse_sequence(config.count, config.tokens, options, true)) {
            fprintf(stderr, "Invalid option in config file: %s\n", options->config_path);
            return false;
        }
    }

    if (!parse_sequence(argc - 1, argv + 1, options, false)) return false;
    timing_resolve(&options->timing_draft, &options->timing);
    return true;
}
