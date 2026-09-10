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
            state->pending_deadline_ns = 0;
            state->last_physical_down_ns = now_ns;
            return DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN;
        }

        /* A delayed run loop must not make the effective hold window longer. */
        return DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN;
    }

    if (state->downstream_down) {
        /* Duplicate Down while downstream already believes the button is held. */
        state->last_physical_down_ns = now_ns;
        return DEBOUNCE_DROP;
    }

    state->downstream_down = true;
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
    if (!state->downstream_down) {
        /* Fail open: an unmatched Up is useful because it releases stuck state. */
        return DEBOUNCE_PASS;
    }

    uint64_t held_ns = UINT64_MAX;
    if (now_ns >= state->last_physical_down_ns) {
        held_ns = now_ns - state->last_physical_down_ns;
    }

    /* Short presses get their own window; every other press uses the normal one. */
    state->pending_up = true;
    state->pending_deadline_ns = now_ns + (held_ns < short0_ns ? hold0_ns : hold_ns);
    return DEBOUNCE_HOLD_UP;
}

void debounce_pending_emitted(DebounceState *state) {
    state->pending_up = false;
    state->pending_deadline_ns = 0;
    state->downstream_down = false;
    state->last_physical_down_ns = 0;
}

void debounce_reset(DebounceState *state) {
    debounce_state_init(state);
}
