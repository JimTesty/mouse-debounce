#include "wheel_analysis.h"

#include <assert.h>
#include <stdio.h>

static void observe_regular(WheelAnalyzer *a, double *t, int n, double gap, int magnitude) {
    for (int i = 0; i < n; ++i) {
        *t += gap;
        WheelDiagnostic d = wheel_analyzer_observe(a, *t, 1, magnitude);
        assert(!d.missing_candidate);
    }
}

int main(void) {
    WheelAnalyzer a;
    wheel_analyzer_init(&a);
    double t = 1000.0;

    (void)wheel_analyzer_observe(&a, t, 1, 1);
    observe_regular(&a, &t, 7, 10.0, 1);

    t += 20.2;
    WheelDiagnostic d = wheel_analyzer_observe(&a, t, 1, 1);
    assert(d.missing_candidate);
    assert(!d.low_confidence);
    assert(d.missing_count == 1);

    observe_regular(&a, &t, 6, 10.0, 4);

    /* A same-direction 4->1 reset can itself be caused by a long missed interval.
       Flag it if cadence fits, but label it low-confidence and reset the model. */
    t += 20.0;
    d = wheel_analyzer_observe(&a, t, 1, 1);
    assert(d.missing_candidate);
    assert(d.low_confidence);
    assert(d.missing_count == 1);

    /* A direction change is a new gesture, not a candidate. */
    t += 20.0;
    d = wheel_analyzer_observe(&a, t, -1, 1);
    assert(!d.missing_candidate);

    puts("wheel analysis tests passed");
    return 0;
}
