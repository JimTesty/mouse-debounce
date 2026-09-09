#ifndef MOUSE_DEBOUNCE_LOGIC_H
#define MOUSE_DEBOUNCE_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DEBOUNCE_PASS = 0,
    DEBOUNCE_HOLD_UP,
    DEBOUNCE_DROP,
    DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN,
    DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN,
} DebounceAction;

typedef struct {
    bool downstream_down;
    bool pending_up;
    uint64_t last_physical_down_ns;
    uint64_t pending_deadline_ns;
} DebounceState;

void debounce_state_init(DebounceState *state);
DebounceAction debounce_on_down(DebounceState *state, uint64_t now_ns);
DebounceAction debounce_on_up(
    DebounceState *state,
    uint64_t now_ns,
    uint64_t short_ns,
    uint64_t hold_ns
);
void debounce_pending_emitted(DebounceState *state);
void debounce_reset(DebounceState *state);

#endif
