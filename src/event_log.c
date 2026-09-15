#include "event_log.h"

#include "config_file.h"
#include "monotonic_clock.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

static const char *click_ordinal_suffix(int64_t click_state) {
    int64_t last_two = click_state % 100;
    if (last_two >= 11 && last_two <= 13) return "th";
    switch (click_state % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

static int button_slot(int64_t button) {
    return button >= 0 && button <= 9 ? (int)button : EVENT_LOG_BUTTON_SLOTS - 1;
}

bool event_log_open(EventLog *log, const char *config_path) {
    memset(log, 0, sizeof(*log));
    char path[CONFIG_PATH_SIZE];
    const char *slash = strrchr(config_path, '/');
    int length = slash != NULL
        ? snprintf(path, sizeof(path), "%.*s/events.log", (int)(slash - config_path), config_path)
        : snprintf(path, sizeof(path), "events.log");
    if (length < 0 || (size_t)length >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    char *name = strrchr(path, '/');
    if (name != NULL && name != path) {
        *name = '\0';
        int result = mkdir(path, 0700);
        *name = '/';
        if (result != 0 && errno != EEXIST) return false;
    }
    log->file = fopen(path, "a");
    if (log->file == NULL) return false;
    (void)chmod(path, 0600);
    setvbuf(log->file, NULL, _IOLBF, 0);
    return true;
}

bool event_log_handle(
    EventLog *log,
    CGEventType type,
    CGEventRef event,
    DebounceAction action,
    const TimingSettings *timing
) {
    if (log->file == NULL) return true;
    bool down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
                type == kCGEventOtherMouseDown;
    bool up = type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp ||
              type == kCGEventOtherMouseUp;
    if (!down && !up && type != kCGEventScrollWheel) return true;

    int64_t button = -1;
    EventLogButton *previous = NULL;
    if (down || up) {
        button = type == kCGEventLeftMouseDown || type == kCGEventLeftMouseUp ? 0 :
            type == kCGEventRightMouseDown || type == kCGEventRightMouseUp ? 1 :
            CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        previous = &log->buttons[button_slot(button)];
    }

    struct timeval wall;
    if (gettimeofday(&wall, NULL) != 0) return false;
    struct tm local;
    if (localtime_r(&wall.tv_sec, &local) == NULL) return false;
    char date[32];
    if (strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local) == 0) return false;

    /* Use uptime for gaps so changes to the wall clock don't split groups. */
    uint64_t now = monotonic_now_ns();
    if (!log->has_event ||
        (down && now - log->last_event_ns > UINT64_C(1000000000))) {
        fputs("-----\n", log->file);
    }
    log->last_event_ns = now;
    log->has_event = true;
    fprintf(log->file, "%s  ", date);
    if (type == kCGEventScrollWheel) {
        fprintf(log->file, "WHEEL  vertical=%" PRId64 " horizontal=%" PRId64 " axis3=%" PRId64 "\n",
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1),
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2),
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3));
    } else {
        char other_name[16];
        const char *name;
        if (button == 0) {
            name = "LEFT";
        } else if (button == 1) {
            name = "RIGHT";
        } else if (button == 2) {
            name = "MIDDLE";
        } else if (button >= 3 && button <= 9) {
            snprintf(other_name, sizeof(other_name), "OTHER%" PRId64, button);
            name = other_name;
        } else {
            name = "OTHERX";
        }
        int64_t click_state = CGEventGetIntegerValueField(event, kCGMouseEventClickState);
        fprintf(log->file, "%-6s %-4s ", name, down ? "down" : "up");
        if (click_state == 0) {
            fputs("dragged  ", log->file);
        } else {
            fprintf(log->file, "%" PRId64 "%s click", click_state,
                click_ordinal_suffix(click_state));
        }
        if (previous->has_event) {
            fprintf(log->file, " (%3.0fms elapsed)", (double)(now - previous->last_event_ns) / 1e6);
        }
        if (up && timing != NULL && button >= 0 && button < MOUSE_BUTTON_COUNT) {
            double short0_ms = timing->short0_ms[button];
            bool short_press = previous->has_down && now >= previous->last_down_ns &&
                (double)(now - previous->last_down_ns) / 1e6 < short0_ms;
            fprintf(log->file, short_press ? " < %.0fms" : " \xE2\x89\xA5 %.0fms", short0_ms);
            previous->pending_uses_hold0 = short_press;
        } else if (up) {
            previous->pending_uses_hold0 = false;
        }
        previous->last_event_ns = now;
        previous->has_event = true;
        if (action == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN) {
            bool uses_hold0 = previous->pending_uses_hold0;
            double hold_ms = timing != NULL && button >= 0 && button < MOUSE_BUTTON_COUNT
                ? (uses_hold0 ? timing->hold0_ms[button] : timing->hold_ms[button]) : 0.0;
            fprintf(log->file, " < %.0fms (%s) -- DISCARDED", hold_ms,
                uses_hold0 ? "hold0-ms" : "hold-ms");
        }
        if (down) {
            previous->has_down = true;
            previous->last_down_ns = now;
            previous->pending_uses_hold0 = false;
        }
        fputc('\n', log->file);
    }
    return !ferror(log->file);
}

void event_log_close(EventLog *log) {
    if (log->file != NULL) fclose(log->file);
    log->file = NULL;
}
