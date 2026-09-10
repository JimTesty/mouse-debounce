#include "event_log.h"

#include "config_file.h"
#include "monotonic_clock.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

typedef struct EventLogButton {
    int64_t number;
    uint64_t last_event_ns;
    struct EventLogButton *next;
} EventLogButton;

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

bool event_log_handle(EventLog *log, CGEventType type, CGEventRef event, DebounceAction action) {
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
        previous = log->buttons;
        while (previous != NULL && previous->number != button) previous = previous->next;
    }

    struct timeval wall;
    if (gettimeofday(&wall, NULL) != 0) return false;
    struct tm local;
    if (localtime_r(&wall.tv_sec, &local) == NULL) return false;
    char date[32];
    if (strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local) == 0) return false;

    /* Use uptime for gaps so changes to the wall clock don't split groups. */
    uint64_t now = monotonic_now_ns();
    if (!log->has_event || ((down || up) && previous == NULL) ||
        (down && now - log->last_event_ns > UINT64_C(1000000000))) {
        fputs("-----\n", log->file);
    }
    log->last_event_ns = now;
    log->has_event = true;
    fprintf(log->file, "%s.%02ld  ", date, (long)(wall.tv_usec / 10000));
    if (type == kCGEventScrollWheel) {
        fprintf(log->file, "WHEEL  vertical=%" PRId64 " horizontal=%" PRId64 " axis3=%" PRId64 "\n",
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1),
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2),
            CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis3));
    } else {
        const char *name = button == 0 ? "LEFT" : button == 1 ? "RIGHT" :
                           button == 2 ? "MIDDLE" : "OTHER";
        fprintf(log->file, "%-6s %-4s button=%" PRId64 " clickState=%" PRId64,
            name, down ? "down" : "up", button,
            CGEventGetIntegerValueField(event, kCGMouseEventClickState));
        if (previous != NULL) {
            fprintf(log->file, " (%.2fms)", (double)(now - previous->last_event_ns) / 1e6);
        } else {
            previous = calloc(1, sizeof(*previous));
            if (previous == NULL) return false;
            previous->number = button;
            previous->next = log->buttons;
            log->buttons = previous;
        }
        previous->last_event_ns = now;
        if (action == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN) {
            fputs("  <<< suspected bounce (Up/Down within hold-ms; pair suppressed)", log->file);
        } else if (action == DEBOUNCE_DROP) {
            fputs("  <<< suspected bounce (duplicate Down; suppressed)", log->file);
        }
        fputc('\n', log->file);
    }
    return !ferror(log->file);
}

void event_log_close(EventLog *log) {
    if (log->file != NULL) fclose(log->file);
    log->file = NULL;
    while (log->buttons != NULL) {
        EventLogButton *next = log->buttons->next;
        free(log->buttons);
        log->buttons = next;
    }
}
