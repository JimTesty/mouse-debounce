#define _DARWIN_C_SOURCE
#include "event_log.h"
#include "debounce_filter.h"
#include "monotonic_clock.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t now_ns;

uint64_t monotonic_now_ns(void) { return now_ns; }
void debounce_sound_play(void) {}

static void record(EventLog *log, CGEventType type, CGEventRef event, uint64_t ns) {
    now_ns = ns;
    assert(event_log_handle(log, type, event, DEBOUNCE_PASS));
}

static void filter_annotations(void) {
    EventLog log = {0};
    log.file = tmpfile();
    assert(log.file != NULL);
    DebounceFilter filter;
    const bool enabled[MOUSE_BUTTON_COUNT] = {true, false, false};
    const double short_ms[MOUSE_BUTTON_COUNT] = {0, 0, 0};
    const double hold_ms[MOUSE_BUTTON_COUNT] = {25, 25, 25};
    debounce_filter_init(&filter, enabled, short_ms, hold_ms);
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, CGPointZero, kCGMouseButtonLeft);
    assert(event != NULL);
    const CGEventType types[] = {kCGEventLeftMouseDown, kCGEventLeftMouseUp,
        kCGEventLeftMouseDown, kCGEventLeftMouseDown, kCGEventRightMouseDown};
    const DebounceAction expected[] = {DEBOUNCE_PASS, DEBOUNCE_HOLD_UP,
        DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN, DEBOUNCE_DROP, DEBOUNCE_PASS};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        now_ns = (10 + i) * 1000000;
        CGEventSetType(event, types[i]);
        DebounceAction action;
        CGEventRef result = debounce_filter_handle(&filter, NULL, types[i], event, &action);
        assert(action == expected[i]);
        assert((result == NULL) == (i >= 1 && i <= 3));
        assert(event_log_handle(&log, types[i], event, action));
    }
    /* No run loop or event posting: cancel any pending synthetic-test resources. */
    debounce_filter_abandon(&filter);
    CFRelease(event);
    rewind(log.file);
    char line[256];
    int lines = 0, notes = 0;
    while (fgets(line, sizeof(line), log.file) != NULL) {
        if (strstr(line, "<<< suspected bounce") != NULL) notes++;
        if (lines == 2) assert(strstr(line, "pair suppressed") != NULL);
        if (lines == 3) assert(strstr(line, "duplicate Down; suppressed") != NULL);
        if (lines == 2 || lines == 3) {
            assert(strstr(line, " (1.00ms)  <<< suspected bounce") != NULL);
        }
        if (lines == 4) assert(strstr(line, "RIGHT") != NULL);
        lines++;
    }
    assert(lines == 5 && notes == 2);
    event_log_close(&log);
}

int main(void) {
    filter_annotations();
    char directory[] = "/tmp/mousedebounce-log-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char config[1024], path[1024], subdirectory[1024];
    snprintf(config, sizeof(config), "%s/config/config.args", directory);
    snprintf(path, sizeof(path), "%s/config/events.log", directory);
    snprintf(subdirectory, sizeof(subdirectory), "%s/config", directory);
    EventLog log;
    assert(event_log_open(&log, config));
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventOtherMouseDown,
                                              CGPointZero, (CGMouseButton)3);
    assert(event != NULL);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 2);
    record(&log, kCGEventLeftMouseDown, event, 0);
    record(&log, kCGEventLeftMouseUp, event, 45670000);

    CGEventRef wheel = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 2, -1, 2);
    assert(wheel != NULL);
    record(&log, kCGEventScrollWheel, wheel, 1200000000); /* No separator before wheel. */
    record(&log, kCGEventRightMouseDown, event, 2000000000); /* Wheel reset idle time. */
    record(&log, kCGEventRightMouseUp, event, 2100000000);
    record(&log, kCGEventOtherMouseDown, event, 3100000000); /* Exactly 1 s: no separator. */
    record(&log, kCGEventOtherMouseUp, event, 3200000000);
    record(&log, kCGEventMouseMoved, event, 4100000000); /* Ignored, including for idle. */
    record(&log, kCGEventLeftMouseDragged, event, 4200000000);
    record(&log, kCGEventLeftMouseDown, event, 4200000001); /* >1 s: separator. */
    event_log_close(&log);
    assert(event_log_open(&log, config));
    record(&log, kCGEventLeftMouseDown, event, 9000000000); /* Append across runs. */
    event_log_close(&log);
    CFRelease(event);
    CFRelease(wheel);

    FILE *file = fopen(path, "r");
    assert(file != NULL);
    char line[256];
    int events = 0, separators = 0;
    bool saw_wheel = false, saw_other = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strcmp(line, "-----\n") == 0) {
            assert(events == 7);
            separators++;
            continue;
        }
        int year, month, day, hour, minute, second, hundredth, length = 0;
        assert(sscanf(line, "%4d-%2d-%2d %2d:%2d:%2d.%2d%n",
            &year, &month, &day, &hour, &minute, &second, &hundredth, &length) == 7);
        assert(length == 22 && line[22] == ' ');
        assert(month >= 1 && month <= 12 && day >= 1 && day <= 31);
        assert(hour >= 0 && hour < 24 && minute >= 0 && minute < 60);
        assert(second >= 0 && second <= 60 && hundredth >= 0 && hundredth < 100);
        if (strstr(line, "WHEEL  vertical=-1 horizontal=2 axis3=0") != NULL) saw_wheel = true;
        if (strstr(line, "OTHER  down button=3 clickState=2") != NULL) saw_other = true;
        if (events == 1) {
            assert(strstr(line, " (45.67ms)\n") != NULL);
        } else if (events == 4 || events == 6) {
            assert(strstr(line, " (100.00ms)\n") != NULL);
        } else if (events == 7) {
            assert(strstr(line, " (4154.33ms)\n") != NULL);
        } else {
            assert(strstr(line, "ms)") == NULL);
        }
        events++;
    }
    assert(events == 9 && separators == 1 && saw_wheel && saw_other);
    assert(!ferror(file));
    fclose(file);
    assert(unlink(path) == 0);
    assert(rmdir(subdirectory) == 0);
    assert(rmdir(directory) == 0);
    puts("event_log tests passed");
    return 0;
}
