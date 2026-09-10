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
    uint64_t short_ns,
    uint64_t hold_ns
) {
    if (!state->downstream_down) {
        /* Fail open: an unmatched Up is useful because it releases stuck state. */
        return DEBOUNCE_PASS;
    }

    uint64_t held_ns = UINT64_MAX;
    if (state->last_physical_down_ns != 0 && now_ns >= state->last_physical_down_ns) {
        held_ns = now_ns - state->last_physical_down_ns;
    }

    /* Zero removes the press-length restriction, catching glitches during long holds. */
    if (short_ns == 0 || held_ns < short_ns) {
        state->pending_up = true;
        state->pending_deadline_ns = now_ns + hold_ns;
        return DEBOUNCE_HOLD_UP;
    }

    state->downstream_down = false;
    state->last_physical_down_ns = 0;
    return DEBOUNCE_PASS;
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
