#ifndef MOUSE_DEBOUNCE_WHEEL_ANALYSIS_H
#define MOUSE_DEBOUNCE_WHEEL_ANALYSIS_H

#include <stdbool.h>
#include <stddef.h>

#define WHEEL_GAP_HISTORY 8

typedef struct {
    int direction;
    int previous_magnitude;
    double previous_time_ms;
    double gaps_ms[WHEEL_GAP_HISTORY];
    size_t gap_count;
    size_t gap_next;
    size_t run_events;
    size_t strong_missing_events;
    size_t weak_missing_events;
} WheelAnalyzer;

typedef struct {
    bool missing_candidate;
    bool low_confidence;
    int missing_count;
    double gap_ms;
    double cadence_ms;
    double cadence_mad_ms;
    double ratio;
} WheelDiagnostic;

void wheel_analyzer_init(WheelAnalyzer *analyzer);
WheelDiagnostic wheel_analyzer_observe(
    WheelAnalyzer *analyzer,
    double now_ms,
    int direction,
    int magnitude
);

#endif
