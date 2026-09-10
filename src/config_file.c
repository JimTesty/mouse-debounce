#include "config_file.h"

#include "mouse_button.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *home_directory(void) {
    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') return pw->pw_dir;
    const char *home = getenv("HOME");
    return home != NULL && home[0] != '\0' ? home : NULL;
}

bool config_default_path(char path[CONFIG_PATH_SIZE]) {
    const char *home = home_directory();
    if (home == NULL) return false;
    int n = snprintf(path, CONFIG_PATH_SIZE,
        "%s/Library/Application Support/MouseDebounce/config.args", home);
    return n > 0 && n < CONFIG_PATH_SIZE;
}

bool config_load_tokens(const char *path, ConfigTokens *tokens) {
    memset(tokens, 0, sizeof(*tokens));
    FILE *f = fopen(path, "r");
    if (f == NULL) return errno == ENOENT;

    char line[1024];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) *hash = '\0';
        for (char *tok = strtok(line, " \t\r\n"); tok != NULL; tok = strtok(NULL, " \t\r\n")) {
            if (tokens->count >= CONFIG_MAX_TOKENS) {
                fclose(f);
                return false;
            }
            size_t len = strlen(tok);
            if (len >= CONFIG_TOKEN_SIZE) {
                fclose(f);
                return false;
            }
            strcpy(tokens->storage[tokens->count], tok);
            tokens->tokens[tokens->count] = tokens->storage[tokens->count];
            tokens->count++;
        }
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static bool ensure_config_directory(const char *path) {
    char dir[CONFIG_PATH_SIZE];
    size_t len = strlen(path);
    if (len >= sizeof(dir)) return false;
    strcpy(dir, path);
    char *slash = strrchr(dir, '/');
    if (slash == NULL) return false;
    *slash = '\0';
    if (mkdir(dir, 0700) == 0 || errno == EEXIST) return true;
    return false;
}

static void write_buttons(FILE *f, const bool buttons[MOUSE_BUTTON_COUNT]) {
    fprintf(f, "--buttons ");
    bool first = true;
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        if (!buttons[i]) continue;
        if (!first) fputc(',', f);
        fputs(mouse_button_cli_name((MouseButton)i), f);
        first = false;
    }
    fputc('\n', f);
}

static bool all_equal(const double v[MOUSE_BUTTON_COUNT]) {
    return v[0] == v[1] && v[1] == v[2];
}

bool config_write_settings(
    const char *path,
    const TimingSettings *timing,
    const bool buttons[MOUSE_BUTTON_COUNT],
    double sound_volume,
    bool debug,
    bool debug_wheel,
    bool log
) {
    if (!ensure_config_directory(path)) return false;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    (void)chmod(path, 0600);

    fprintf(f, "# MouseDebounce filter settings. CLI arguments override these.\n");
    write_buttons(f, buttons);
    fprintf(f, "--sound-volume %.3g\n", sound_volume);
    if (debug) fprintf(f, "--debug\n");
    if (debug_wheel) fprintf(f, "--debug-wheel\n");
    if (log) fprintf(f, "--log\n");

    if (all_equal(timing->short0_ms)) {
        fprintf(f, "--short0-ms %.3g\n", timing->short0_ms[0]);
    } else {
        for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
            fprintf(f, "--%s-short0-ms %.3g\n",
                mouse_button_cli_name((MouseButton)i), timing->short0_ms[i]);
        }
    }

    if (all_equal(timing->hold0_ms)) {
        fprintf(f, "--hold0-ms %.3g\n", timing->hold0_ms[0]);
    } else {
        for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
            fprintf(f, "--%s-hold0-ms %.3g\n",
                mouse_button_cli_name((MouseButton)i), timing->hold0_ms[i]);
        }
    }

    if (all_equal(timing->hold_ms)) {
        fprintf(f, "--hold-ms %.3g\n", timing->hold_ms[0]);
    } else {
        for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
            fprintf(f, "--%s-hold-ms %.3g\n",
                mouse_button_cli_name((MouseButton)i), timing->hold_ms[i]);
        }
    }

    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}
