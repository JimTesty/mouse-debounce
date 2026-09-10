#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "options.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void invalid_arguments(void) {
    const char *invalid[] = {"nan", "inf", "-inf", "0", "-1", "1e30", "20ms", ""};
    AppOptions options;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char *args[] = {"test", "--no-config", "--short-ms", (char *)invalid[i]};
        assert(!options_parse(4, args, &options));
    }
    char *missing[] = {"test", "--no-config", "--hold-ms"};
    assert(!options_parse(3, missing, &options));
    char *unknown[] = {"test", "--no-config", "--unknown"};
    assert(!options_parse(3, unknown, &options));
    char *duration[] = {"test", "--no-config", "--duration", "nan"};
    assert(!options_parse(4, duration, &options));
    duration[3] = "inf";
    assert(!options_parse(4, duration, &options));
    duration[3] = "0";
    assert(options_parse(4, duration, &options));
    assert(!options.debug);
    char *debug[] = {"test", "--no-config", "--debug", "--sound-volume", "0"};
    assert(options_parse(5, debug, &options));
    assert(options.debug && options.sound_volume == 0);
    debug[4] = "1";
    assert(options_parse(5, debug, &options));
    debug[4] = "1.1";
    assert(!options_parse(5, debug, &options));
    debug[4] = "nan";
    assert(!options_parse(5, debug, &options));

    bool buttons[MOUSE_BUTTON_COUNT];
    assert(mouse_parse_button_list("left,middle", buttons));
    assert(buttons[0] && !buttons[1] && buttons[2]);
    const char *bad_lists[] = {"", ",left", "left,", "left,,right", "other"};
    for (size_t i = 0; i < sizeof(bad_lists) / sizeof(bad_lists[0]); ++i) {
        assert(!mouse_parse_button_list(bad_lists[i], buttons));
    }
}

static void precedence_and_roundtrip(void) {
    char directory[] = "/tmp/mousedebounce-options-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char path[CONFIG_PATH_SIZE];
    assert(snprintf(path, sizeof(path), "%s/config.args", directory) > 0);
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("# Config precedes CLI\n--left-short-ms 18\n--hold-ms 21\n", file) >= 0);
    assert(fclose(file) == 0);

    AppOptions options;
    char *args[] = {"test", "--config", path, "--right-short-ms", "24",
                    "--hold-ms", "30", "--left-hold-ms", "12.5"};
    assert(options_parse(9, args, &options));
    assert(options.timing.short_ms[0] == 18);
    assert(options.timing.short_ms[1] == 24);
    assert(options.timing.short_ms[2] == 21);
    assert(options.timing.hold_ms[0] == 12.5);
    assert(options.timing.hold_ms[1] == 30);
    assert(options.timing.hold_ms[2] == 30);
    options.buttons[MOUSE_BUTTON_RIGHT] = false;
    options.sound_volume = 0.25;
    assert(config_write_settings(path, &options.timing, options.buttons, options.sound_volume));
    AppOptions loaded;
    char *load_args[] = {"test", "--config", path};
    assert(options_parse(3, load_args, &loaded));
    assert(loaded.sound_volume == 0.25);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        assert(loaded.timing.short_ms[i] == options.timing.short_ms[i]);
        assert(loaded.timing.hold_ms[i] == options.timing.hold_ms[i]);
        assert(loaded.buttons[i] == options.buttons[i]);
    }
    char *ignore[] = {"test", "--config", path, "--no-config"};
    assert(options_parse(4, ignore, &loaded));
    assert(loaded.timing.short_ms[0] == DEFAULT_SHORT_MS);
    file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("--short-ms invalid\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(!options_parse(3, load_args, &loaded));
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

int main(void) {
    invalid_arguments();
    precedence_and_roundtrip();
    puts("options/config tests passed");
    return 0;
}
