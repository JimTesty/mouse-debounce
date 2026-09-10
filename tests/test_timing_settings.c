#include "timing_settings.h"

#include <assert.h>
#include <stdio.h>

static void defaults(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_resolve(&d, &s);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        assert(s.short0_ms[i] == 50.0);
        assert(s.hold0_ms[i] == 40.0);
        assert(s.hold_ms[i] == 25.0);
    }
}

static void sibling_inheritance(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_set_short0_button(&d, MOUSE_BUTTON_LEFT, 10.0);
    timing_set_short0_button(&d, MOUSE_BUTTON_RIGHT, 30.0);
    timing_set_hold0_button(&d, MOUSE_BUTTON_LEFT, 60.0);
    timing_set_hold0_button(&d, MOUSE_BUTTON_RIGHT, 80.0);
    timing_set_hold_button(&d, MOUSE_BUTTON_MIDDLE, 17.0);
    timing_resolve(&d, &s);
    assert(s.short0_ms[MOUSE_BUTTON_LEFT] == 10.0);
    assert(s.short0_ms[MOUSE_BUTTON_RIGHT] == 30.0);
    assert(s.short0_ms[MOUSE_BUTTON_MIDDLE] == 20.0);
    assert(s.hold0_ms[MOUSE_BUTTON_LEFT] == 60.0);
    assert(s.hold0_ms[MOUSE_BUTTON_RIGHT] == 80.0);
    assert(s.hold0_ms[MOUSE_BUTTON_MIDDLE] == 70.0);
    assert(s.hold_ms[MOUSE_BUTTON_LEFT] == 17.0);
    assert(s.hold_ms[MOUSE_BUTTON_RIGHT] == 17.0);
    assert(s.hold_ms[MOUSE_BUTTON_MIDDLE] == 17.0);
}

static void explicit_zero_is_set(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_set_short0_button(&d, MOUSE_BUTTON_LEFT, 0.0);
    timing_set_short0_button(&d, MOUSE_BUTTON_RIGHT, 30.0);
    timing_resolve(&d, &s);
    assert(s.short0_ms[MOUSE_BUTTON_LEFT] == 0.0);
    assert(s.short0_ms[MOUSE_BUTTON_RIGHT] == 30.0);
    assert(s.short0_ms[MOUSE_BUTTON_MIDDLE] == 15.0);
}

static void global_then_override(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_set_short0_all(&d, 20.0);
    timing_set_short0_button(&d, MOUSE_BUTTON_MIDDLE, 25.0);
    timing_set_hold0_all(&d, 60.0);
    timing_set_hold0_button(&d, MOUSE_BUTTON_LEFT, 55.0);
    timing_set_hold_all(&d, 21.0);
    timing_resolve(&d, &s);
    assert(s.short0_ms[MOUSE_BUTTON_LEFT] == 20.0);
    assert(s.short0_ms[MOUSE_BUTTON_RIGHT] == 20.0);
    assert(s.short0_ms[MOUSE_BUTTON_MIDDLE] == 25.0);
    assert(s.hold0_ms[MOUSE_BUTTON_LEFT] == 55.0);
    assert(s.hold0_ms[MOUSE_BUTTON_RIGHT] == 60.0);
    assert(s.hold0_ms[MOUSE_BUTTON_MIDDLE] == 60.0);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) assert(s.hold_ms[i] == 21.0);
}

int main(void) {
    defaults();
    sibling_inheritance();
    explicit_zero_is_set();
    global_then_override();
    puts("timing_settings tests passed");
    return 0;
}
