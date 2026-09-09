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
}

int main(void) {
    normal_click();
    bounce_pair();
    suspicious_up_times_out();
    overdue_down_is_not_bounce();
    unmatched_up_fails_open();
    puts("debounce_logic tests passed");
    return 0;
}
