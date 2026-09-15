#include "debounce_logic.h"

#include <string.h>

void debounce_state_init(DebounceState *state) {
    memset(state, 0, sizeof(*state));
}

DebounceAction debounce_on_down(DebounceState *state, uint64_t now_ns) {
    if (state->pending_up) {
        if (now_ns < state->pending_deadline_ns) {
            /* The withheld Up + returning Down are one bounce pair. */
            state->pending_up = false;
            state->pending_uses_hold0 = false;
            state->pending_deadline_ns = 0;
            state->has_physical_down = true;
            state->last_physical_down_ns = now_ns;
            return DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN;
        }

        /* A delayed run loop must not make the effective hold window longer. */
        return DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN;
    }

    /* Every physical Down becomes the reference, even if Downs repeat. */
    state->has_physical_down = true;
    state->last_physical_down_ns = now_ns;
    return DEBOUNCE_PASS;
}

DebounceAction debounce_on_up(
    DebounceState *state,
    uint64_t now_ns,
    uint64_t short0_ns,
    uint64_t hold0_ns,
    uint64_t hold_ns
) {
    /* No previous Down behaves like an infinitely old one and selects hold_ns. */
    uint64_t held_ns = UINT64_MAX;
    if (state->has_physical_down && now_ns >= state->last_physical_down_ns) {
        held_ns = now_ns - state->last_physical_down_ns;
    }

    /* Short presses get their own window; every other press uses the normal one. */
    state->pending_uses_hold0 = held_ns < short0_ns;
    state->pending_up = true;
    state->pending_deadline_ns = now_ns +
        (state->pending_uses_hold0 ? hold0_ns : hold_ns);
    return DEBOUNCE_HOLD_UP;
}

void debounce_pending_emitted(DebounceState *state) {
    state->pending_up = false;
    state->pending_uses_hold0 = false;
    state->pending_deadline_ns = 0;
    /* Keep the latest Down because physical events need not alternate. */
}
