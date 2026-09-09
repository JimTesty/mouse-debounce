#include "config_file.h"
#include "debounce_filter.h"
#include "event_tap.h"
#include "measurement.h"
#include "mouse_events.h"
#include "options.h"
#include "permissions.h"
#include "signal_bridge.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    AppOptions options;
    EventTap event_tap;
    DebounceFilter filter;
    Measurement measurement;
    SignalBridge signals;
    FILE *output;
    CFRunLoopTimerRef duration_timer;
    CFRunLoopTimerRef permission_timer;
    bool stopping;
    bool permission_lost;
    bool pid_file_written;
} App;

static CGEventRef app_event_handler(
    void *context,
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event
) {
    App *app = (App *)context;
    if (app->options.mode == APP_MODE_MEASURE) {
        measurement_handle(&app->measurement, type, event);
        return event;
    }
    return debounce_filter_handle(&app->filter, proxy, type, event);
}

static void app_tap_reset(void *context) {
    App *app = (App *)context;
    if (app->options.mode == APP_MODE_FILTER) {
        debounce_filter_reset_safely(&app->filter);
    }
}

static void app_stop(App *app) {
    if (app->stopping) return;
    app->stopping = true;

    if (app->options.mode == APP_MODE_FILTER) {
        debounce_filter_flush(&app->filter);
    } else {
        measurement_print_summary(&app->measurement);
    }

    CFRunLoopStop(CFRunLoopGetCurrent());
}

static void app_stop_for_permission_loss(App *app) {
    if (app->stopping) return;
    app->stopping = true;
    app->permission_lost = true;

    /* Stop intercepting input before doing any other cleanup. */
    event_tap_stop(&app->event_tap);
    fprintf(app->output, "Accessibility permission was revoked; exiting immediately.\n");
    CFRunLoopStop(CFRunLoopGetCurrent());
}

static void signal_stop(void *context) {
    app_stop((App *)context);
}

static void duration_timer_callback(CFRunLoopTimerRef timer, void *info) {
    (void)timer;
    app_stop((App *)info);
}

static void permission_timer_callback(CFRunLoopTimerRef timer, void *info) {
    (void)timer;
    App *app = (App *)info;
    if (!permissions_has_accessibility()) app_stop_for_permission_loss(app);
}

static bool setup_duration_timer(App *app) {
    if (app->options.duration_seconds <= 0.0) return true;

    CFRunLoopTimerContext ctx = {0};
    ctx.info = app;
    app->duration_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + app->options.duration_seconds,
        0.0,
        0,
        0,
        duration_timer_callback,
        &ctx
    );
    if (app->duration_timer == NULL) return false;
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), app->duration_timer, kCFRunLoopCommonModes);
    return true;
}

static bool setup_permission_timer(App *app) {
    CFRunLoopTimerContext ctx = {0};
    ctx.info = app;
    app->permission_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 2.0,
        2.0,
        0,
        0,
        permission_timer_callback,
        &ctx
    );
    if (app->permission_timer == NULL) return false;
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), app->permission_timer, kCFRunLoopCommonModes);
    return true;
}

static bool write_pid_file(App *app) {
    if (app->options.pid_file == NULL) return true;
    FILE *f = fopen(app->options.pid_file, "w");
    if (f == NULL) return false;
    fprintf(f, "%ld\n", (long)getpid());
    bool ok = !ferror(f) && fclose(f) == 0;
    if (!ok) return false;
    app->pid_file_written = true;
    return true;
}

static void remove_pid_file(App *app) {
    if (!app->pid_file_written || app->options.pid_file == NULL) return;
    (void)unlink(app->options.pid_file);
    app->pid_file_written = false;
}

static void cleanup(App *app) {
    remove_pid_file(app);
    if (app->duration_timer != NULL) {
        CFRunLoopTimerInvalidate(app->duration_timer);
        CFRelease(app->duration_timer);
        app->duration_timer = NULL;
    }
    if (app->permission_timer != NULL) {
        CFRunLoopTimerInvalidate(app->permission_timer);
        CFRelease(app->permission_timer);
        app->permission_timer = NULL;
    }

    event_tap_stop(&app->event_tap);
    signal_bridge_stop(&app->signals);

    if (app->options.mode == APP_MODE_FILTER) {
        /* Permission loss forbids reposting; ordinary shutdown releases held Ups. */
        if (app->permission_lost) debounce_filter_abandon(&app->filter);
        else debounce_filter_destroy(&app->filter);
    }

    if (app->output != NULL && app->output != stdout) {
        fclose(app->output);
        app->output = NULL;
    }
}

int main(int argc, char **argv) {
    App app;
    memset(&app, 0, sizeof(app));
    app.signals.pipe_fd[0] = -1;
    app.signals.pipe_fd[1] = -1;
    app.output = stdout;

    if (!options_parse(argc, argv, &app.options)) {
        options_print_usage(argv[0]);
        return 2;
    }

    if (app.options.output_path != NULL) {
        app.output = fopen(app.options.output_path, "w");
        if (app.output == NULL) {
            perror("Could not open measurement output");
            return 1;
        }
        setvbuf(app.output, NULL, _IOLBF, 0);
    } else {
        setvbuf(stdout, NULL, _IOLBF, 0);
    }

    if (app.options.save_config_and_exit) {
        if (!config_write_settings(
                app.options.config_path,
                &app.options.timing,
                app.options.buttons)) {
            fprintf(app.output, "Could not save config: %s\n", app.options.config_path);
            cleanup(&app);
            return 1;
        }
        fprintf(app.output, "Saved config: %s\n", app.options.config_path);
        cleanup(&app);
        return 0;
    }

    if (!write_pid_file(&app)) {
        fprintf(app.output, "Could not write pid file: %s\n", app.options.pid_file);
        cleanup(&app);
        return 1;
    }

    if (!permissions_request_accessibility()) {
        fprintf(app.output,
            "Mouse Debounce needs Accessibility permission.\n"
            "Enable Mouse Debounce in System Settings > Privacy & Security > Accessibility,\n"
            "then relaunch it. Input Monitoring is not required.\n");
        cleanup(&app);
        return 1;
    }

    CGEventMask mask = mouse_button_event_mask();
    if (app.options.mode == APP_MODE_MEASURE) mask |= CGEventMaskBit(kCGEventScrollWheel);

    if (app.options.mode == APP_MODE_FILTER) {
        debounce_filter_init(
            &app.filter,
            app.options.buttons,
            app.options.timing.short_ms,
            app.options.timing.hold_ms
        );
    } else {
        measurement_init(&app.measurement, app.options.buttons, app.output);
    }

    if (!event_tap_start(
            &app.event_tap,
            mask,
            app_event_handler,
            app_tap_reset,
            &app)) {
        fprintf(app.output,
            "Could not create CGEventTap. Check Privacy & Security permissions.\n");
        cleanup(&app);
        return 1;
    }

    if (!setup_permission_timer(&app)) {
        fprintf(app.output, "Could not create Accessibility watchdog timer.\n");
        cleanup(&app);
        return 1;
    }

    if (!signal_bridge_start(&app.signals, signal_stop, &app)) {
        fprintf(app.output,
            "Warning: SIGINT/SIGTERM cleanup bridge unavailable; continuing.\n");
    }

    if (!setup_duration_timer(&app)) {
        fprintf(app.output, "Could not create duration timer.\n");
        cleanup(&app);
        return 1;
    }

    if (app.options.mode == APP_MODE_MEASURE) {
        measurement_print_instructions(&app.measurement, app.options.duration_seconds);
    } else {
        fprintf(app.output, "Mouse Debounce active:\n");
        for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
            if (!app.options.buttons[button]) continue;
            fprintf(app.output, "  %-6s short-ms=%.1f hold-ms=%.1f\n",
                mouse_button_name((MouseButton)button),
                app.options.timing.short_ms[button],
                app.options.timing.hold_ms[button]);
        }
        if (app.options.use_config) {
            fprintf(app.output, "Config: %s\n", app.options.config_path);
        }
    }

    CFRunLoopRun();
    cleanup(&app);
    return 0;
}
