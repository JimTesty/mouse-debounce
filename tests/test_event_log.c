#define _DARWIN_C_SOURCE
#include "event_log.h"
#include "debounce_filter.h"
#include "monotonic_clock.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t now_ns;
static CGEventRef posted_event;
static const TimingSettings timing = {
    .short0_ms = {50, 50, 50},
    .hold0_ms = {70, 70, 70},
    .hold_ms = {25, 25, 25},
};

/* Capture replay without submitting synthetic input to the OS. */
void CGEventPost(CGEventTapLocation tap, CGEventRef event) {
    (void)tap;
    assert(posted_event == NULL);
    posted_event = CGEventCreateCopy(event);
}

uint64_t monotonic_now_ns(void) { return now_ns; }
void debounce_sound_play(void) {}

static void record(EventLog *log, CGEventType type, CGEventRef event, uint64_t ns) {
    now_ns = ns;
    assert(event_log_handle(log, type, event, DEBOUNCE_PASS, &timing));
}

static void filter_annotations(void) {
    EventLog log = {0};
    log.file = tmpfile();
    assert(log.file != NULL);
    DebounceFilter filter;
    const bool enabled[MOUSE_BUTTON_COUNT] = {true, false, false};
    debounce_filter_init(&filter, enabled, timing.short0_ms, timing.hold0_ms, timing.hold_ms);
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, CGPointZero, kCGMouseButtonLeft);
    assert(event != NULL);
    const CGEventType types[] = {kCGEventLeftMouseDown, kCGEventLeftMouseUp,
        kCGEventLeftMouseDown, kCGEventLeftMouseDown, kCGEventRightMouseDown};
    const DebounceAction expected[] = {DEBOUNCE_PASS, DEBOUNCE_HOLD_UP,
        DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN, DEBOUNCE_PASS, DEBOUNCE_PASS};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        now_ns = (10 + i) * 1000000;
        CGEventSetType(event, types[i]);
        DebounceAction action;
        CFAbsoluteTime before = CFAbsoluteTimeGetCurrent();
        CGEventRef result = debounce_filter_handle(&filter, types[i], event, &action);
        CFAbsoluteTime after = CFAbsoluteTimeGetCurrent();
        if (i == 1) {
            CFAbsoluteTime fire = CFRunLoopTimerGetNextFireDate(filter.button[0].pending_timer);
            assert(fire >= before + 0.070 - 0.000001 && fire <= after + 0.070 + 0.000001);
        }
        assert(action == expected[i]);
        assert((result == NULL) == (i >= 1 && i <= 2));
        assert(event_log_handle(&log, types[i], event, action, &timing));
    }
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 0);
    now_ns = 100 * 1000000;
    CGEventSetType(event, kCGEventLeftMouseUp);
    DebounceAction action;
    assert(debounce_filter_handle(&filter, kCGEventLeftMouseUp, event, &action) == NULL);
    assert(action == DEBOUNCE_HOLD_UP);
    assert(event_log_handle(&log, kCGEventLeftMouseUp, event, action, &timing));
    now_ns = 110 * 1000000;
    CGEventSetType(event, kCGEventLeftMouseDown);
    assert(debounce_filter_handle(&filter, kCGEventLeftMouseDown, event, &action) == NULL);
    assert(action == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    assert(event_log_handle(&log, kCGEventLeftMouseDown, event, action, &timing));
    /* No run loop or event posting: cancel any pending synthetic-test resources. */
    debounce_filter_abandon(&filter);
    CFRelease(event);
    rewind(log.file);
    char line[256];
    int lines = 0, notes = 0;
    while (fgets(line, sizeof(line), log.file) != NULL) {
        if (strcmp(line, "-----\n") == 0) {
            assert(lines == 0);
            continue;
        }
        if (strstr(line, "DISCARDED") != NULL) notes++;
        if (lines == 2) {
            assert(strstr(line, " (  1ms elapsed) < 70ms (hold0-ms) -- DISCARDED") != NULL);
        }
        if (lines == 1) assert(strstr(line, " (  1ms elapsed) < 50ms") != NULL);
        if (lines == 5) {
            assert(strstr(line, "LEFT   up   dragged  ") != NULL);
            assert(strstr(line, " ( 87ms elapsed) \xE2\x89\xA5 50ms") != NULL);
        }
        if (lines == 6) assert(strstr(line, " ( 10ms elapsed) < 25ms (hold-ms) -- DISCARDED") != NULL);
        if (lines == 4) assert(strstr(line, "RIGHT") != NULL);
        lines++;
    }
    assert(lines == 7 && notes == 2);
    /* A delayed release must retain the original event time and coordinates. */
    event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown,
                                   CGPointMake(123, 456), kCGMouseButtonLeft);
    assert(event != NULL);
    debounce_filter_handle(&filter, kCGEventLeftMouseDown, event, &action);
    CGEventSetType(event, kCGEventLeftMouseUp);
    CGEventSetTimestamp(event, 123456789);
    assert(debounce_filter_handle(&filter, kCGEventLeftMouseUp, event, &action) == NULL);
    CGEventSetTimestamp(event, 987654321);
    CGEventSetLocation(event, CGPointMake(789, 987));
    debounce_filter_flush(&filter);
    assert(posted_event != NULL);
    assert(CGEventGetTimestamp(posted_event) == 123456789);
    assert(CGPointEqualToPoint(CGEventGetLocation(posted_event), CGPointMake(123, 456)));
    CFRelease(posted_event);
    posted_event = NULL;
    CFRelease(event);
    debounce_filter_destroy(&filter);
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
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 3);
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
            assert(events == 0 || events == 7 || events == 8);
            separators++;
            continue;
        }
        int year, month, day, hour, minute, second, length = 0;
        assert(sscanf(line, "%4d-%2d-%2d %2d:%2d:%2d%n",
            &year, &month, &day, &hour, &minute, &second, &length) == 6);
        assert(length == 19 && line[19] == ' ');
        assert(month >= 1 && month <= 12 && day >= 1 && day <= 31);
        assert(hour >= 0 && hour < 24 && minute >= 0 && minute < 60);
        assert(second >= 0 && second <= 60);
        if (strstr(line, "WHEEL  vertical=-1 horizontal=2 axis3=0") != NULL) saw_wheel = true;
        if (strstr(line, "OTHER3 down 3rd click") != NULL) saw_other = true;
        if (events == 1) {
            assert(strstr(line, " ( 46ms elapsed) < 50ms\n") != NULL);
        } else if (events == 4 || events == 6) {
            assert(strstr(line, " (100ms elapsed)") != NULL);
            if (events == 4) assert(strstr(line, " \xE2\x89\xA5 50ms\n") != NULL);
        } else if (events == 7) {
            assert(strstr(line, " (4154ms elapsed)\n") != NULL);
        } else {
            assert(strstr(line, "ms)") == NULL);
        }
        events++;
    }
    assert(events == 9 && separators == 3 && saw_wheel && saw_other);
    assert(!ferror(file));
    fclose(file);
    assert(unlink(path) == 0);
    assert(rmdir(subdirectory) == 0);
    assert(rmdir(directory) == 0);
    puts("event_log tests passed");
    return 0;
}
