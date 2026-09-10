#include "measurement.h"

#include <assert.h>
#include <string.h>

static uint64_t now_ns;
static unsigned ticks;

/* Test receipt times and alert requests without posting events or playing audio. */
uint64_t monotonic_now_ns(void) { return now_ns; }
double monotonic_ns_to_ms(uint64_t ns) { return (double)ns / 1000000.0; }
void debounce_sound_play(void) { ++ticks; }

static void observe(Measurement *m, CGEventRef event, CGEventType type, unsigned ms) {
    now_ns = (uint64_t)ms * 1000000;
    measurement_handle(m, type, event);
}

int main(void) {
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
    puts("measurement tests passed");
    return 0;
}
