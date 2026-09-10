#include "debounce_logic.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define MS(x) ((uint64_t)((x) * 1000000.0 + 0.5))

static void combined_windows(void) {
    DebounceState s;
    debounce_state_init(&s);
    /* Initial chatter: 38.49 ms press, then a 31.98 ms release gap. */
    assert(debounce_on_down(&s, 0) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(38.49), MS(50), MS(70), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(s.pending_deadline_ns == MS(108.49));
    assert(debounce_on_down(&s, MS(70.47)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    /* A returning physical Down resets the short-press clock too. */
    assert(debounce_on_up(&s, MS(100), MS(50), MS(70), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(s.pending_deadline_ns == MS(170));
    assert(debounce_on_down(&s, MS(160)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    /* Long-hold interruption uses the smaller window, in the same press. */
    assert(debounce_on_up(&s, MS(1000), MS(50), MS(70), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(s.pending_deadline_ns == MS(1025));
    assert(debounce_on_down(&s, MS(1020)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    assert(debounce_on_up(&s, MS(1200), MS(50), MS(70), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(debounce_on_down(&s, MS(1232)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&s);
    assert(debounce_on_down(&s, MS(1232)) == DEBOUNCE_PASS);
    assert(debounce_on_down(&s, MS(1233)) == DEBOUNCE_DROP);
}

static void threshold_boundaries(void) {
    DebounceState s;
    debounce_state_init(&s);
    assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(150), MS(50), MS(40), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(s.pending_deadline_ns == MS(175)); /* Exactly short0 uses normal hold. */
    assert(debounce_on_down(&s, MS(175)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&s);
    assert(debounce_on_down(&s, MS(175)) == DEBOUNCE_PASS);
    assert(debounce_on_up(&s, MS(200), MS(50), MS(40), MS(25)) == DEBOUNCE_HOLD_UP);
    assert(s.pending_deadline_ns == MS(240));
    assert(debounce_on_down(&s, MS(240)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&s);
    assert(!s.downstream_down && !s.pending_up);
}

static void zero_short0_uses_normal_window(void) {
    const uint64_t durations[] = {MS(10), MS(1000), MS(10000)};
    for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        DebounceState s;
        debounce_state_init(&s);
        assert(debounce_on_up(&s, MS(1), 0, MS(70), MS(25)) == DEBOUNCE_PASS);
        assert(debounce_on_down(&s, MS(100)) == DEBOUNCE_PASS);
        uint64_t up = MS(100) + durations[i];
        assert(debounce_on_up(&s, up, 0, MS(70), MS(25)) == DEBOUNCE_HOLD_UP);
        assert(s.pending_deadline_ns == up + MS(25));
        assert(debounce_on_down(&s, up + MS(24)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
    }
}

static void independent_windows(void) {
    DebounceState left, right;
    debounce_state_init(&left);
    debounce_state_init(&right);
    assert(debounce_on_up(&left, MS(1), MS(50), MS(40), MS(25)) == DEBOUNCE_PASS);
    debounce_on_down(&left, MS(100));
    debounce_on_down(&right, MS(100));
    debounce_on_up(&left, MS(120), MS(50), MS(40), MS(25));
    debounce_on_up(&right, MS(120), 0, MS(70), MS(10));
    assert(left.pending_deadline_ns == MS(160));
    assert(right.pending_deadline_ns == MS(130));
    assert(debounce_on_down(&right, MS(131)) == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN);
    debounce_pending_emitted(&right);
    assert(left.pending_up && left.pending_deadline_ns == MS(160));
    assert(debounce_on_down(&left, MS(150)) == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN);
}

int main(void) {
    combined_windows();
    threshold_boundaries();
    zero_short0_uses_normal_window();
    independent_windows();
    puts("debounce_logic tests passed");
    return 0;
}
