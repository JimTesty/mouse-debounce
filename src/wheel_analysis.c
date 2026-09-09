#include "wheel_analysis.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double median(double *values, size_t n) {
    qsort(values, n, sizeof(values[0]), compare_double);
    if (n & 1u) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

static bool cadence(const WheelAnalyzer *a, double *median_ms, double *mad_ms) {
    if (a->gap_count < 5) return false;

    double values[WHEEL_GAP_HISTORY];
    size_t n = a->gap_count < WHEEL_GAP_HISTORY ? a->gap_count : WHEEL_GAP_HISTORY;
    for (size_t i = 0; i < n; ++i) values[i] = a->gaps_ms[i];
    double m = median(values, n);
    if (m <= 0.0) return false;

    double deviations[WHEEL_GAP_HISTORY];
    for (size_t i = 0; i < n; ++i) deviations[i] = fabs(a->gaps_ms[i] - m);
    double mad = median(deviations, n);

    *median_ms = m;
    *mad_ms = mad;
    return true;
}

static void clear_run(WheelAnalyzer *a, int direction, int magnitude, double now_ms) {
    a->direction = direction;
    a->previous_magnitude = magnitude;
    a->previous_time_ms = now_ms;
    a->gap_count = 0;
    a->gap_next = 0;
    a->run_events = 1;
}

static void add_gap(WheelAnalyzer *a, double gap_ms) {
    a->gaps_ms[a->gap_next] = gap_ms;
    a->gap_next = (a->gap_next + 1) % WHEEL_GAP_HISTORY;
    if (a->gap_count < WHEEL_GAP_HISTORY) a->gap_count++;
}

void wheel_analyzer_init(WheelAnalyzer *analyzer) {
    memset(analyzer, 0, sizeof(*analyzer));
}

WheelDiagnostic wheel_analyzer_observe(
    WheelAnalyzer *a,
    double now_ms,
    int direction,
    int magnitude
) {
    WheelDiagnostic result;
    memset(&result, 0, sizeof(result));

    if (direction == 0 || magnitude <= 0) {
        clear_run(a, 0, 0, now_ms);
        return result;
    }

    if (a->previous_time_ms <= 0.0 || a->direction == 0) {
        clear_run(a, direction, magnitude, now_ms);
        return result;
    }

    double gap = now_ms - a->previous_time_ms;
    result.gap_ms = gap;

    if (gap <= 0.0 || gap > 1000.0 || direction != a->direction) {
        clear_run(a, direction, magnitude, now_ms);
        return result;
    }

    bool acceleration_reset = magnitude <= 1 && a->previous_magnitude >= 3;
    double base = 0.0, mad = 0.0;
    bool have_cadence = cadence(a, &base, &mad);

    if (have_cadence) {
        result.cadence_ms = base;
        result.cadence_mad_ms = mad;
        result.ratio = gap / base;

        bool stable = mad <= base * 0.28 + 0.5;
        long multiple = lround(result.ratio);
        if (stable && multiple >= 2 && multiple <= 10) {
            double expected = (double)multiple * base;
            double residual = fabs(gap - expected);
            double tolerance = fmax(base * 0.18, 2.5 * mad + 0.75);
            if (gap >= base * 1.85 && residual <= tolerance) {
                result.missing_candidate = true;
                result.low_confidence = acceleration_reset;
                result.missing_count = (int)multiple - 1;
                if (acceleration_reset) {
                    a->weak_missing_events += (size_t)result.missing_count;
                } else {
                    a->strong_missing_events += (size_t)result.missing_count;
                }
            }
        }

        if (acceleration_reset) {
            /* Could be a new gesture, or a long miss that reset macOS acceleration. */
            clear_run(a, direction, magnitude, now_ms);
            return result;
        }

        if (result.missing_candidate) {
            /* Keep the pre-gap cadence; do not pollute it with the enlarged gap. */
        } else if (gap > base * 3.5) {
            clear_run(a, direction, magnitude, now_ms);
            return result;
        } else {
            add_gap(a, gap);
        }
    } else {
        if (acceleration_reset) {
            clear_run(a, direction, magnitude, now_ms);
            return result;
        }
        add_gap(a, gap);
    }

    a->direction = direction;
    a->previous_magnitude = magnitude;
    a->previous_time_ms = now_ms;
    a->run_events++;
    return result;
}
