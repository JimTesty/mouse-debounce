#include "measurement.h"

#include <assert.h>
#include <string.h>

static uint64_t now_ns;
static unsigned ticks;

/* Test receipt times and alert requests without posting events or playing audio. */
uint64_t monotonic_now_ns(void) { return now_ns; }
double monotonic_ns_to_ms(uint64_t ns) { return (double)ns / 1000000.0; }
void debounce_sound_play(void) { ++ticks; }

static void observe(Measurement *m, CGEventRef event, CGEventType type, double ms) {
    now_ns = (uint64_t)(ms * 1000000.0 + 0.5);
    measurement_handle(m, type, event);
}

static void short_press_alerts(void) {
    static Measurement m;
    bool enabled[MOUSE_BUTTON_COUNT] = {true, false, false};
    TimingSettings timing = {{20, 20, 20}, {20, 20, 20}};
    FILE *out = tmpfile();
    assert(out != NULL);
    measurement_init(&m, enabled, &timing, out);
    CGEventRef event = CGEventCreate(NULL);
    assert(event != NULL);

    observe(&m, event, kCGEventLeftMouseDown, 100);
    observe(&m, event, kCGEventLeftMouseUp, 108);
    assert(ticks == 0); /* A short press alone is not a bounce. */
    observe(&m, event, kCGEventLeftMouseDown, 115);
    assert(ticks == 1);
    observe(&m, event, kCGEventLeftMouseUp, 150);
    observe(&m, event, kCGEventLeftMouseDown, 200);
    observe(&m, event, kCGEventLeftMouseUp, 208);
    observe(&m, event, kCGEventLeftMouseDown, 228); /* Exactly at expiry: new click. */
    assert(ticks == 1);
    observe(&m, event, kCGEventLeftMouseDown, 229); /* Duplicate Down. */
    assert(ticks == 2);
    observe(&m, event, kCGEventRightMouseDown, 230); /* Disabled button. */
    assert(ticks == 2);
    assert(m.press_count[0] == 3 && m.gap_count[0] == 4);
    measurement_print_summary(&m);

    rewind(out);
    char line[1024];
    unsigned marked = 0;
    bool summary = false;
    while (fgets(line, sizeof(line), out) != NULL) {
        if (strstr(line, "suspected bounce") != NULL) {
            if (marked == 0) assert(strncmp(line, "\033[1;33m", 7) == 0);
            else assert(strncmp(line, "\033[1m", 4) == 0);
            assert(strstr(line, "\033[0m\n") != NULL);
            ++marked;
        }
        if (strstr(line, "Measurement summary") != NULL) summary = true;
    }
    assert(marked == 2 && summary);
    CFRelease(event);
    fclose(out);
}

static void reported_long_hold_bounces(double short_ms) {
    static Measurement m;
    bool enabled[MOUSE_BUTTON_COUNT] = {true, false, false};
    TimingSettings timing = {{short_ms, short_ms, short_ms}, {70, 70, 70}};
    FILE *out = tmpfile();
    assert(out != NULL);
    measurement_init(&m, enabled, &timing, out);
    CGEventRef event = CGEventCreate(NULL);
    assert(event != NULL);
    ticks = 0;

    /* User's raw intervals: 993.6/23.9, 177.5/20.4, then 40.1/198.0 ms. */
    observe(&m, event, kCGEventLeftMouseDown, 1000);
    observe(&m, event, kCGEventLeftMouseUp, 1993.6);
    observe(&m, event, kCGEventLeftMouseDown, 2017.5);
    observe(&m, event, kCGEventLeftMouseUp, 2195);
    observe(&m, event, kCGEventLeftMouseDown, 2215.4);
    observe(&m, event, kCGEventLeftMouseUp, 2255.5);
    observe(&m, event, kCGEventLeftMouseDown, 2453.5);
    observe(&m, event, kCGEventLeftMouseUp, 2512.8);
    unsigned expected = short_ms == 0 ? 2 : 0;
    assert(ticks == expected);
    assert(m.press_count[0] == 4 && m.gap_count[0] == 3);
    measurement_print_summary(&m);

    rewind(out);
    char line[1024];
    unsigned marked = 0;
    bool preserved_setting = false;
    while (fgets(line, sizeof(line), out) != NULL) {
        if (strstr(line, "suspected bounce") != NULL) {
            assert(strncmp(line, "\033[1;33m", 7) == 0);
            assert(strstr(line, "Up/Down within hold-ms") != NULL);
            assert(strstr(line, "\033[0m\n") != NULL);
            ++marked;
        }
        if (strstr(line, short_ms == 0 ? "--left-short-ms 0 " : "--left-short-ms 50 ")) {
            preserved_setting = true;
        }
    }
    assert(marked == expected && preserved_setting);
    CFRelease(event);
    fclose(out);
}

static void independent_buttons(void) {
    static Measurement m;
    bool enabled[MOUSE_BUTTON_COUNT] = {true, true, true};
    TimingSettings timing = {{0, 50, 0}, {70, 30, 10}};
    FILE *out = tmpfile();
    assert(out != NULL);
    measurement_init(&m, enabled, &timing, out);
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventOtherMouseDown,
        CGPointZero, kCGMouseButtonCenter);
    assert(event != NULL);
    ticks = 0;

    observe(&m, event, kCGEventLeftMouseDown, 100);
    observe(&m, event, kCGEventRightMouseDown, 101);
    observe(&m, event, kCGEventOtherMouseDown, 102);
    observe(&m, event, kCGEventLeftMouseUp, 1100);
    observe(&m, event, kCGEventRightMouseUp, 1101);
    observe(&m, event, kCGEventOtherMouseUp, 1102);
    assert(m.shadow[0].pending_up && !m.shadow[1].pending_up && m.shadow[2].pending_up);

    observe(&m, event, kCGEventOtherMouseDown, 1110); /* Middle: 8 < 10 ms. */
    assert(ticks == 1 && m.shadow[0].pending_up);
    observe(&m, event, kCGEventRightMouseDown, 1111); /* Long right press was released. */
    assert(ticks == 1);
    observe(&m, event, kCGEventRightMouseUp, 1121); /* Short right press now gets a window. */
    observe(&m, event, kCGEventLeftMouseDown, 1124); /* Left: 24 < 70 ms. */
    assert(ticks == 2 && m.shadow[1].pending_up);
    observe(&m, event, kCGEventRightMouseDown, 1151); /* Right: exactly 30 ms, new press. */
    assert(ticks == 2 && !m.shadow[1].pending_up);
    assert(m.shadow[0].downstream_down && m.shadow[2].downstream_down);

    CFRelease(event);
    fclose(out);
}

int main(void) {
    short_press_alerts();
    reported_long_hold_bounces(0);
    reported_long_hold_bounces(50);
    independent_buttons();
    puts("measurement tests passed");
    return 0;
}
