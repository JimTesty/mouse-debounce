#ifndef MOUSE_DEBOUNCE_STATISTICS_H
#define MOUSE_DEBOUNCE_STATISTICS_H

#include <stdbool.h>
#include <stddef.h>

#define STATS_MAX_SAMPLES 4096

typedef struct {
    size_t count;
    size_t outliers_removed;
    double min;
    double max;
    double mean;
    double median;
    double p90;
    double q1;
    double q3;
} StatsSummary;

typedef struct {
    bool clear_split;
    double suggested_threshold_ms;
    double low_max_ms;
    double high_min_ms;
    size_t low_count;
    size_t high_count;
    size_t low_outliers_removed;
    StatsSummary low_stats;
    StatsSummary high_stats;
} ThresholdAnalysis;

bool stats_summary_iqr(
    const double *samples,
    size_t count,
    StatsSummary *summary,
    double *cleaned,
    size_t *cleaned_count
);

ThresholdAnalysis stats_analyze_threshold(
    const double *samples,
    size_t count,
    double analysis_window_ms
);

#endif
