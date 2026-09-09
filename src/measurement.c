#include "measurement.h"

#include "statistics.h"
#include "timing_settings.h"

#include <inttypes.h>
#include <string.h>

#define ANALYSIS_WINDOW_MS 250.0

static void add_sample(double *samples, size_t *count, double value) {
    if (*count < MEASUREMENT_MAX_SAMPLES) samples[(*count)++] = value;
}

void measurement_init(
    Measurement *measurement,
    const bool enabled[MOUSE_BUTTON_COUNT],
    FILE *out
) {
    memset(measurement, 0, sizeof(*measurement));
    measurement->out = out != NULL ? out : stdout;
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) measurement->enabled[i] = enabled[i];
}

static void handle_button(Measurement *m, CGEventType type, CGEventRef event) {
    MouseButtonEvent mouse;
    if (!mouse_button_from_event(type, event, &mouse) || !m->enabled[mouse.button]) return;

    uint64_t now = CGEventGetTimestamp(event);
    if (m->first_ns == 0) m->first_ns = now;
    double elapsed_s = now >= m->first_ns ? (double)(now - m->first_ns) / 1e9 : 0.0;
    int64_t click_state = CGEventGetIntegerValueField(event, kCGMouseEventClickState);

    if (mouse.is_down) {
        double gap = -1.0;
        if (m->last_up_ns[mouse.button] != 0 && now >= m->last_up_ns[mouse.button]) {
            gap = (double)(now - m->last_up_ns[mouse.button]) / 1e6;
            add_sample(m->gap_ms[mouse.button], &m->gap_count[mouse.button], gap);
        }
        m->last_down_ns[mouse.button] = now;
        if (gap >= 0.0) {
            fprintf(m->out,
                "%9.3fs  %-6s down   gap-from-up=%8.1f ms  clickState=%" PRId64 "\n",
                elapsed_s, mouse_button_name(mouse.button), gap, click_state);
        } else {
            fprintf(m->out,
                "%9.3fs  %-6s down   gap-from-up=       -  clickState=%" PRId64 "\n",
                elapsed_s, mouse_button_name(mouse.button), click_state);
        }
    } else {
        double held = -1.0;
        if (m->last_down_ns[mouse.button] != 0 && now >= m->last_down_ns[mouse.button]) {
            held = (double)(now - m->last_down_ns[mouse.button]) / 1e6;
            add_sample(m->press_ms[mouse.button], &m->press_count[mouse.button], held);
        }
        m->last_up_ns[mouse.button] = now;
        if (held >= 0.0) {
            fprintf(m->out,
                "%9.3fs  %-6s up     press-held=%8.1f ms  clickState=%" PRId64 "\n",
                elapsed_s, mouse_button_name(mouse.button), held, click_state);
        } else {
            fprintf(m->out,
                "%9.3fs  %-6s up     press-held=       -  clickState=%" PRId64 "\n",
                elapsed_s, mouse_button_name(mouse.button), click_state);
        }
    }
    fflush(m->out);
}

static void handle_scroll(Measurement *m, CGEventRef event) {
    uint64_t now = CGEventGetTimestamp(event);
    if (m->first_ns == 0) m->first_ns = now;
    double elapsed_s = now >= m->first_ns ? (double)(now - m->first_ns) / 1e9 : 0.0;
    double gap_ms = m->last_scroll_ns != 0 && now >= m->last_scroll_ns
        ? (double)(now - m->last_scroll_ns) / 1e6
        : -1.0;
    m->last_scroll_ns = now;
    m->scroll_count++;

    int64_t line_v = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
    int64_t line_h = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
    int64_t point_v = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1);
    int64_t point_h = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2);
    int64_t continuous = CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous);

    if (gap_ms >= 0.0) {
        fprintf(m->out,
            "%9.3fs  WHEEL   v=%4" PRId64 " h=%4" PRId64
            " point=(%4" PRId64 ",%4" PRId64 ") gap=%7.1f ms continuous=%" PRId64 "\n",
            elapsed_s, line_v, line_h, point_v, point_h, gap_ms, continuous);
    } else {
        fprintf(m->out,
            "%9.3fs  WHEEL   v=%4" PRId64 " h=%4" PRId64
            " point=(%4" PRId64 ",%4" PRId64 ") gap=      - continuous=%" PRId64 "\n",
            elapsed_s, line_v, line_h, point_v, point_h, continuous);
    }
    fflush(m->out);
}

void measurement_handle(Measurement *measurement, CGEventType type, CGEventRef event) {
    if (type == kCGEventScrollWheel) {
        handle_scroll(measurement, event);
        return;
    }
    handle_button(measurement, type, event);
}

static void print_stats_line(FILE *out, const char *label, const double *samples, size_t count) {
    double window[STATS_MAX_SAMPLES];
    size_t n = 0;
    for (size_t i = 0; i < count && n < STATS_MAX_SAMPLES; ++i) {
        if (samples[i] >= 0.0 && samples[i] <= ANALYSIS_WINDOW_MS) window[n++] = samples[i];
    }
    if (n == 0) {
        fprintf(out, "  %s: no samples <= %.0f ms\n", label, ANALYSIS_WINDOW_MS);
        return;
    }

    StatsSummary s;
    double clean[STATS_MAX_SAMPLES];
    size_t clean_n = 0;
    if (!stats_summary_iqr(window, n, &s, clean, &clean_n)) return;
    fprintf(out,
        "  %s <= %.0f ms: n=%zu, IQR-outliers=%zu, mean=%.1f, median=%.1f,"
        " p90=%.1f, range=%.1f..%.1f ms\n",
        label, ANALYSIS_WINDOW_MS, s.count, s.outliers_removed,
        s.mean, s.median, s.p90, s.min, s.max);
}

static void print_threshold_analysis(FILE *out, const char *label, ThresholdAnalysis a) {
    if (!a.clear_split) {
        fprintf(out, "  %s: no reliable low/high timing split found; no direct recommendation.\n", label);
        return;
    }
    fprintf(out,
        "  %s low cluster: n=%zu, IQR-outliers=%zu, mean=%.1f, median=%.1f,"
        " p90=%.1f, max=%.1f ms; next cluster starts at %.1f ms\n",
        label, a.low_stats.count, a.low_stats.outliers_removed,
        a.low_stats.mean, a.low_stats.median, a.low_stats.p90,
        a.low_max_ms, a.high_min_ms);
    fprintf(out, "    heuristic threshold: %.0f ms\n", a.suggested_threshold_ms);
}

static const char *inheritance_label(bool measured, int measured_siblings) {
    if (measured) return "measured";
    if (measured_siblings > 0) return "sibling average";
    return "default";
}

void measurement_print_summary(Measurement *m) {
    fprintf(m->out, "\nMeasurement summary\n-------------------\n");

    ThresholdAnalysis press_analysis[MOUSE_BUTTON_COUNT];
    ThresholdAnalysis gap_analysis[MOUSE_BUTTON_COUNT];
    memset(press_analysis, 0, sizeof(press_analysis));
    memset(gap_analysis, 0, sizeof(gap_analysis));

    TimingDraft suggested;
    timing_draft_init(&suggested);

    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        fprintf(m->out, "%s (%zu press, %zu release->down samples):\n",
            mouse_button_name((MouseButton)button),
            m->press_count[button], m->gap_count[button]);
        print_stats_line(m->out, "press durations", m->press_ms[button], m->press_count[button]);
        print_stats_line(m->out, "release->next-down gaps", m->gap_ms[button], m->gap_count[button]);

        press_analysis[button] = stats_analyze_threshold(
            m->press_ms[button], m->press_count[button], ANALYSIS_WINDOW_MS);
        gap_analysis[button] = stats_analyze_threshold(
            m->gap_ms[button], m->gap_count[button], ANALYSIS_WINDOW_MS);
        print_threshold_analysis(m->out, "short-ms", press_analysis[button]);
        print_threshold_analysis(m->out, "hold-ms", gap_analysis[button]);

        if (press_analysis[button].clear_split) {
            timing_set_short_button(&suggested, (MouseButton)button,
                press_analysis[button].suggested_threshold_ms);
        }
        if (gap_analysis[button].clear_split) {
            timing_set_hold_button(&suggested, (MouseButton)button,
                gap_analysis[button].suggested_threshold_ms);
        }
    }

    TimingSettings resolved;
    timing_resolve(&suggested, &resolved);

    fprintf(m->out,
        "\nSuggested settings\n------------------\n"
        "These are heuristic: the tool looks for a separated low-timing cluster, removes\n"
        "Tukey-IQR outliers within that cluster, and places the threshold halfway to the\n"
        "next cluster. Missing buttons inherit measured siblings; otherwise they use 20 ms.\n");

    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        int short_siblings = 0, hold_siblings = 0;
        for (int other = 0; other < MOUSE_BUTTON_COUNT; ++other) {
            if (other == button) continue;
            if (press_analysis[other].clear_split) short_siblings++;
            if (gap_analysis[other].clear_split) hold_siblings++;
        }
        fprintf(m->out, "  %-6s short-ms=%5.0f (%s), hold-ms=%5.0f (%s)\n",
            mouse_button_name((MouseButton)button),
            resolved.short_ms[button],
            inheritance_label(press_analysis[button].clear_split, short_siblings),
            resolved.hold_ms[button],
            inheritance_label(gap_analysis[button].clear_split, hold_siblings));
    }

    fprintf(m->out, "\nSuggested config/CLI arguments:\n  ");
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        fprintf(m->out, "--%s-short-ms %.0f --%s-hold-ms %.0f ",
            mouse_button_cli_name((MouseButton)button), resolved.short_ms[button],
            mouse_button_cli_name((MouseButton)button), resolved.hold_ms[button]);
    }
    fprintf(m->out, "\n");

    fprintf(m->out,
        "\nWHEEL: %zu scroll events observed. A completely missing wheel event leaves no\n"
        "observable evidence, so this tool does not invent replacement wheel events.\n"
        "Inspect the wheel trace for isolated reverse ticks or erratic deltas instead.\n",
        m->scroll_count);
    fflush(m->out);
}
