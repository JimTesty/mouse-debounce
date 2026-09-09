#include "monotonic_clock.h"

#include <time.h>

uint64_t monotonic_now_ns(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

double monotonic_ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}
