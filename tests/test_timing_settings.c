#include "timing_settings.h"

#include <assert.h>
#include <stdio.h>

static void defaults(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_resolve(&d, &s);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        assert(s.short_ms[i] == 20.0);
        assert(s.hold_ms[i] == 20.0);
    }
}

static void sibling_inheritance(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_set_short_button(&d, MOUSE_BUTTON_LEFT, 10.0);
    timing_set_short_button(&d, MOUSE_BUTTON_RIGHT, 30.0);
    timing_set_hold_button(&d, MOUSE_BUTTON_MIDDLE, 17.0);
    timing_resolve(&d, &s);
    assert(s.short_ms[MOUSE_BUTTON_LEFT] == 10.0);
    assert(s.short_ms[MOUSE_BUTTON_RIGHT] == 30.0);
    assert(s.short_ms[MOUSE_BUTTON_MIDDLE] == 20.0);
    assert(s.hold_ms[MOUSE_BUTTON_LEFT] == 17.0);
    assert(s.hold_ms[MOUSE_BUTTON_RIGHT] == 17.0);
    assert(s.hold_ms[MOUSE_BUTTON_MIDDLE] == 17.0);
}

static void global_then_override(void) {
    TimingDraft d;
    TimingSettings s;
    timing_draft_init(&d);
    timing_set_short_all(&d, 20.0);
    timing_set_short_button(&d, MOUSE_BUTTON_MIDDLE, 25.0);
    timing_set_hold_all(&d, 21.0);
    timing_resolve(&d, &s);
    assert(s.short_ms[MOUSE_BUTTON_LEFT] == 20.0);
    assert(s.short_ms[MOUSE_BUTTON_RIGHT] == 20.0);
    assert(s.short_ms[MOUSE_BUTTON_MIDDLE] == 25.0);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) assert(s.hold_ms[i] == 21.0);
}

int main(void) {
    defaults();
    sibling_inheritance();
    global_then_override();
    puts("timing_settings tests passed");
    return 0;
}
