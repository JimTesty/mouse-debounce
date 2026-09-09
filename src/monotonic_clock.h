#ifndef MOUSE_DEBOUNCE_MONOTONIC_CLOCK_H
#define MOUSE_DEBOUNCE_MONOTONIC_CLOCK_H

#include <stdint.h>

/*
 * Nanoseconds from a monotonic uptime clock.
 *
 * We intentionally timestamp events when the event-tap callback receives them
 * instead of interpreting CGEventTimestamp.  Apple exposes an ns-valued uptime
 * clock directly, so there is no architecture-specific timebase constant here.
 */
uint64_t monotonic_now_ns(void);

double monotonic_ns_to_ms(uint64_t ns);

#endif
