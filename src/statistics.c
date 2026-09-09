#include "statistics.h"

#include <stdlib.h>
#include <string.h>

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double percentile_sorted(const double *v, size_t n, double p) {
    if (n == 0) return 0.0;
    if (n == 1) return v[0];
    double pos = p * (double)(n - 1);
    size_t lo = (size_t)pos;
    size_t hi = lo + 1 < n ? lo + 1 : lo;
    double frac = pos - (double)lo;
    return v[lo] + (v[hi] - v[lo]) * frac;
}

static void fill_summary_sorted(const double *v, size_t n, size_t removed, StatsSummary *s) {
    memset(s, 0, sizeof(*s));
    if (n == 0) return;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += v[i];
    s->count = n;
    s->outliers_removed = removed;
    s->min = v[0];
    s->max = v[n - 1];
    s->mean = sum / (double)n;
    s->median = percentile_sorted(v, n, 0.5);
    s->p90 = percentile_sorted(v, n, 0.9);
    s->q1 = percentile_sorted(v, n, 0.25);
    s->q3 = percentile_sorted(v, n, 0.75);
}

bool stats_summary_iqr(
    const double *samples,
    size_t count,
    StatsSummary *summary,
    double *cleaned,
    size_t *cleaned_count
) {
    if (summary == NULL || cleaned == NULL || cleaned_count == NULL || count == 0) return false;
    if (count > STATS_MAX_SAMPLES) count = STATS_MAX_SAMPLES;

    double sorted[STATS_MAX_SAMPLES];
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (samples[i] >= 0.0) sorted[n++] = samples[i];
    }
    if (n == 0) return false;
    qsort(sorted, n, sizeof(sorted[0]), compare_double);

    double low_fence = sorted[0];
    double high_fence = sorted[n - 1];
    if (n >= 4) {
        double q1 = percentile_sorted(sorted, n, 0.25);
        double q3 = percentile_sorted(sorted, n, 0.75);
        double iqr = q3 - q1;
        if (iqr > 0.0) {
            low_fence = q1 - 1.5 * iqr;
            high_fence = q3 + 1.5 * iqr;
        }
    }

    size_t kept = 0;
    for (size_t i = 0; i < n; ++i) {
        if (sorted[i] >= low_fence && sorted[i] <= high_fence) cleaned[kept++] = sorted[i];
    }
    if (kept == 0) return false;

    fill_summary_sorted(cleaned, kept, n - kept, summary);
    *cleaned_count = kept;
    return true;
}

static double round_ms(double x) {
    if (x <= 1.0) return 1.0;
    return (double)((unsigned long)(x + 0.5));
}

ThresholdAnalysis stats_analyze_threshold(
    const double *samples,
    size_t count,
    double analysis_window_ms
) {
    ThresholdAnalysis result;
    memset(&result, 0, sizeof(result));

    if (count > STATS_MAX_SAMPLES) count = STATS_MAX_SAMPLES;
    double sorted[STATS_MAX_SAMPLES];
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (samples[i] >= 0.0 && samples[i] <= analysis_window_ms) sorted[n++] = samples[i];
    }
    if (n < 4) return result;
    qsort(sorted, n, sizeof(sorted[0]), compare_double);

    size_t best = 0;
    double best_gap = 0.0;
    for (size_t i = 2; i + 2 <= n; ++i) {
        double low = sorted[i - 1];
        double high = sorted[i];
        double gap = high - low;
        if (gap > best_gap) {
            best_gap = gap;
            best = i;
        }
    }
    if (best == 0) return result;

    double raw_low_max = sorted[best - 1];
    double raw_high_min = sorted[best];
    bool plausible_low_cluster = raw_low_max <= 80.0;
    bool separated = best_gap >= 5.0 &&
        (raw_high_min >= raw_low_max * 1.45 || best_gap >= 18.0);
    if (!plausible_low_cluster || !separated) return result;

    double low_clean[STATS_MAX_SAMPLES];
    double high_clean[STATS_MAX_SAMPLES];
    size_t low_n = 0, high_n = 0;
    if (!stats_summary_iqr(sorted, best, &result.low_stats, low_clean, &low_n)) return result;
    if (!stats_summary_iqr(sorted + best, n - best, &result.high_stats, high_clean, &high_n)) return result;
    if (low_n == 0 || high_n == 0) return result;

    result.low_count = low_n;
    result.high_count = high_n;
    result.low_outliers_removed = result.low_stats.outliers_removed;
    result.low_max_ms = low_clean[low_n - 1];
    result.high_min_ms = high_clean[0];
    if (result.high_min_ms <= result.low_max_ms) return result;

    result.suggested_threshold_ms = round_ms((result.low_max_ms + result.high_min_ms) / 2.0);
    result.clear_split = true;
    return result;
}
