#include "debounce_logic.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define MS(x) ((uint64_t)(x) * UINT64_C(1000000))

static void normal_click(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(250), MS(80), MS(70)) == DEBOUNCE_PASS);
    assert(!s.downstream_down);
}

static void bounce_pair(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(130), MS(80), MS(70)) == DEBOUNCE_HOLD_UP);
    assert(debounce_on_down(&s, MS(160)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    assert(s.downstream_down);
    assert(!s.pending_up);
    assert(debounce_on_up(&s, MS(300), MS(80), MS(70)) == DEBOUNCE_PASS);
}

static void suspicious_up_times_out(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(140), MS(80), MS(70)) == DEBOUNCE_HOLD_UP);
    debounce_pending_emitted(&s);
    assert(!s.downstream_down && !s.pending_up);
}

static void overdue_down_is_not_bounce(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(130), MS(80), MS(70)) == DEBOUNCE_HOLD_UP);
    assert(debounce_on_down(&s, MS(205)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&s);
    assert(debounce_on_down(&s, MS(205)) == DEBOUNCE_PASS);
}

static void unmatched_up_fails_open(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_up(&s, MS(100), MS(80), MS(70)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(100), 0, MS(70)) == DEBOUNCE_PASS);
}

static void zero_short_covers_any_press_length(void) {
    const uint64_t durations[] = {MS(10), MS(1000), MS(10000)};
    for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        DebounceState s;
        debounce_state_init(&s);
        uint64_t up = MS(100) + durations[i];
        assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
        assert(debounce_on_up(&s, up, 0, MS(70)) == DEBOUNCE_HOLD_UP);
        assert(s.downstream_down && s.pending_deadline_ns == up + MS(70));
        assert(debounce_on_down(&s, up + MS(24)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
        assert(s.downstream_down && !s.pending_up);
        assert(debounce_on_up(&s, up + MS(200), 0, MS(70)) == DEBOUNCE_HOLD_UP);
        assert(debounce_on_down(&s, up + MS(220)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
        assert(debounce_on_up(&s, up + MS(400), 0, MS(70)) == DEBOUNCE_HOLD_UP);
        debounce_pending_emitted(&s);
        assert(!s.downstream_down && !s.pending_up);
    }
}

static void threshold_boundaries(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(150), MS(50), MS(70)) == DEBOUNCE_PASS);

    assert(debounce_on_down(&s, MS(200)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(1200), 0, MS(70)) == DEBOUNCE_HOLD_UP);
    assert(debounce_on_down(&s, MS(1270)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&s);
    assert(debounce_on_down(&s, MS(1270)) == DEBOUNCE_PASS);
}

int main(void) {
    normal_click();
    bounce_pair();
    suspicious_up_times_out();
    overdue_down_is_not_bounce();
    unmatched_up_fails_open();
    zero_short_covers_any_press_length();
    threshold_boundaries();
    puts("debounce_logic tests passed");
    return 0;
}
