#include "statistics.h"

#include <assert.h>
#include <stdio.h>

static void iqr_removes_gross_outlier(void) {
    const double v[] = {9, 10, 10, 11, 12, 100};
    StatsSummary s;
    double clean[STATS_MAX_SAMPLES];
    size_t n = 0;
    assert(stats_summary_iqr(v, 6, &s, clean, &n));
    assert(n == 5);
    assert(s.outliers_removed == 1);
    assert(s.max == 12.0);
}

static void split_detects_bounce_cluster(void) {
    const double v[] = {5, 7, 8, 10, 55, 60, 75, 90};
    ThresholdAnalysis a = stats_analyze_threshold(v, 8, 250.0);
    assert(a.clear_split);
    assert(a.low_max_ms == 10.0);
    assert(a.high_min_ms == 55.0);
    assert(a.suggested_threshold_ms == 33.0);
}

static void no_fake_split_for_uniform_data(void) {
    const double v[] = {40, 44, 48, 52, 56, 60, 64, 68};
    ThresholdAnalysis a = stats_analyze_threshold(v, 8, 250.0);
    assert(!a.clear_split);
}

int main(void) {
    iqr_removes_gross_outlier();
    split_detects_bounce_cluster();
    no_fake_split_for_uniform_data();
    puts("statistics tests passed");
    return 0;
}
