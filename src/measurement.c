#include "measurement.h"

#include "monotonic_clock.h"
#include "debounce_sound.h"
#include "statistics.h"
#include "timing_settings.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define ANALYSIS_WINDOW_MS 250.0

static void add_sample(double *samples, size_t *count, double value) {
    if (*count < MEASUREMENT_MAX_SAMPLES) samples[(*count)++] = value;
}

static double elapsed_seconds(const Measurement *m, uint64_t now_ns) {
    if (m->first_ns == 0 || now_ns < m->first_ns) return 0.0;
    return (double)(now_ns - m->first_ns) / 1e9;
}

static double interval_ms(uint64_t newer_ns, uint64_t older_ns) {
    if (older_ns == 0 || newer_ns < older_ns) return -1.0;
    return monotonic_ns_to_ms(newer_ns - older_ns);
}

void measurement_init(
    Measurement *measurement,
    const bool enabled[MOUSE_BUTTON_COUNT],
    const TimingSettings *timing,
    FILE *out
) {
    memset(measurement, 0, sizeof(*measurement));
    measurement->out = out != NULL ? out : stdout;
    measurement->timing = *timing;
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) measurement->enabled[i] = enabled[i];
    wheel_analyzer_init(&measurement->wheel_vertical);
    wheel_analyzer_init(&measurement->wheel_horizontal);
}

void measurement_print_instructions(Measurement *m, double duration_seconds) {
    fprintf(m->out,
        "Measurement uses CLOCK_UPTIME_RAW at event-tap receipt; CGEvent timestamps are not\n"
        "used for debounce timing. Nothing is modified.\n"
        "Suspected button bounce is marked in bold and sounds a tick (volume 0 mutes).\n"
        "These are events the current filter settings would suppress, not proof of a fault.\n"
        "Presses shorter than short0-ms use hold0-ms; all others use hold-ms.\n"
        "Ctrl-C ends the session and prints the same summary as the timer.\n\n"
        "Suggested test%s:\n"
        "  1. LEFT:   ~10 normal clicks, ~5 double-clicks, ~5 short/long drags.\n"
        "  2. RIGHT:  same if practical.\n"
        "  3. MIDDLE: ~10 clicks if you use it.\n"
        "  4. WHEEL:  scroll smoothly in ONE direction for >=1 s at roughly steady speed,\n"
        "             then repeat the other direction; also do a few ordinary scroll bursts.\n"
        "     Wheel miss detection is intentionally conservative and only trusts stable runs.\n"
        "     If the mouse behaves perfectly during this session, button calibration may not\n"
        "     reveal chatter timings; rerun measurement when the fault is actually present.\n\n",
        duration_seconds > 0.0 ? " during this timed session" : "");
    if (duration_seconds > 0.0) {
        fprintf(m->out, "This measurement will auto-exit after %.1f seconds.\n\n", duration_seconds);
    }
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (m->enabled[button]) {
            fprintf(m->out, "  %-6s detection: short0-ms=%.1f hold0-ms=%.1f hold-ms=%.1f\n",
                mouse_button_name((MouseButton)button),
                m->timing.short0_ms[button], m->timing.hold0_ms[button], m->timing.hold_ms[button]);
        }
    }
    fputc('\n', m->out);
    fflush(m->out);
}

static DebounceAction button_filter_action(Measurement *m, MouseButtonEvent mouse, uint64_t now_ns) {
    DebounceState *state = &m->shadow[mouse.button];
    /* Simulate timer expiry before this event, without posting or withholding input. */
    if (state->pending_up && now_ns >= state->pending_deadline_ns) {
        debounce_pending_emitted(state);
    }
    return mouse.is_down
        ? debounce_on_down(state, now_ns)
        : debounce_on_up(state, now_ns,
            (uint64_t)(m->timing.short0_ms[mouse.button] * 1000000.0 + 0.5),
            (uint64_t)(m->timing.hold0_ms[mouse.button] * 1000000.0 + 0.5),
            (uint64_t)(m->timing.hold_ms[mouse.button] * 1000000.0 + 0.5));
}

static void handle_button(Measurement *m, CGEventType type, CGEventRef event, uint64_t now_ns) {
    MouseButtonEvent mouse;
    if (!mouse_button_from_event(type, event, &mouse) || !m->enabled[mouse.button]) return;

    if (m->first_ns == 0) m->first_ns = now_ns;
    double elapsed_s = elapsed_seconds(m, now_ns);
    int64_t click_state = CGEventGetIntegerValueField(event, kCGMouseEventClickState);
    DebounceAction action = button_filter_action(m, mouse, now_ns);
    bool bounce_pair = action == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN;
    bool bounce = bounce_pair || action == DEBOUNCE_DROP;
    if (bounce) {
        fputs(bounce_pair ? "\033[1;33m" : "\033[1m", m->out);
        debounce_sound_play();
    }

    if (mouse.is_down) {
        double gap = interval_ms(now_ns, m->last_up_ns[mouse.button]);
        if (gap >= 0.0) add_sample(m->gap_ms[mouse.button], &m->gap_count[mouse.button], gap);
        m->last_down_ns[mouse.button] = now_ns;
        if (gap >= 0.0) {
            fprintf(m->out,
                "%9.3fs  %-6s down   gap-from-up=%8.1f ms  clickState=%" PRId64,
                elapsed_s, mouse_button_name(mouse.button), gap, click_state);
        } else {
            fprintf(m->out,
                "%9.3fs  %-6s down   gap-from-up=       -  clickState=%" PRId64,
                elapsed_s, mouse_button_name(mouse.button), click_state);
        }
    } else {
        double held = interval_ms(now_ns, m->last_down_ns[mouse.button]);
        if (held >= 0.0) add_sample(m->press_ms[mouse.button], &m->press_count[mouse.button], held);
        m->last_up_ns[mouse.button] = now_ns;
        if (held >= 0.0) {
            fprintf(m->out,
                "%9.3fs  %-6s up     press-held=%8.1f ms  clickState=%" PRId64,
                elapsed_s, mouse_button_name(mouse.button), held, click_state);
        } else {
            fprintf(m->out,
                "%9.3fs  %-6s up     press-held=       -  clickState=%" PRId64,
                elapsed_s, mouse_button_name(mouse.button), click_state);
        }
    }
    if (bounce) {
        fprintf(m->out, "  <<< suspected bounce (%s; filter would suppress)\033[0m",
            bounce_pair ? "Up/Down within selected hold window" : "duplicate Down");
    }
    fputc('\n', m->out);
    fflush(m->out);
}

static int sign_i64(int64_t v) {
    return (v > 0) - (v < 0);
}

static int magnitude_i64(int64_t v) {
    if (v < 0) v = -v;
    if (v > 2147483647) return 2147483647;
    return (int)v;
}

static void handle_scroll(Measurement *m, CGEventRef event, uint64_t now_ns) {
    if (m->first_ns == 0) m->first_ns = now_ns;
    double elapsed_s = elapsed_seconds(m, now_ns);
    double gap_ms = interval_ms(now_ns, m->last_scroll_ns);
    m->last_scroll_ns = now_ns;
    m->scroll_count++;

    int64_t line_v = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
    int64_t line_h = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
    int64_t point_v = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1);
    int64_t point_h = CGEventGetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2);
    int64_t continuous = CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous);

    WheelDiagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(diagnostic));
    const char *axis = NULL;
    if (continuous == 0) {
        if (line_v != 0 || point_v != 0) {
            int64_t source = point_v != 0 ? point_v : line_v;
            int64_t magnitude_source = line_v != 0 ? line_v : point_v;
            diagnostic = wheel_analyzer_observe(
                &m->wheel_vertical,
                monotonic_ns_to_ms(now_ns),
                sign_i64(source),
                magnitude_i64(magnitude_source));
            axis = "V";
        } else if (line_h != 0 || point_h != 0) {
            int64_t source = point_h != 0 ? point_h : line_h;
            int64_t magnitude_source = line_h != 0 ? line_h : point_h;
            diagnostic = wheel_analyzer_observe(
                &m->wheel_horizontal,
                monotonic_ns_to_ms(now_ns),
                sign_i64(source),
                magnitude_i64(magnitude_source));
            axis = "H";
        }
    }

    if (gap_ms >= 0.0) {
        fprintf(m->out,
            "%9.3fs  WHEEL   v=%4" PRId64 " h=%4" PRId64
            " point=(%4" PRId64 ",%4" PRId64 ") gap=%7.1f ms continuous=%" PRId64,
            elapsed_s, line_v, line_h, point_v, point_h, gap_ms, continuous);
    } else {
        fprintf(m->out,
            "%9.3fs  WHEEL   v=%4" PRId64 " h=%4" PRId64
            " point=(%4" PRId64 ",%4" PRId64 ") gap=      - continuous=%" PRId64,
            elapsed_s, line_v, line_h, point_v, point_h, continuous);
    }

    if (diagnostic.missing_candidate) {
        fprintf(m->out,
            "  <<< %s-%s-miss=%d (local cadence %.1f ms, ratio %.2fx)",
            diagnostic.low_confidence ? "possible" : "probable",
            axis != NULL ? axis : "wheel",
            diagnostic.missing_count,
            diagnostic.cadence_ms,
            diagnostic.ratio);
    }
    fputc('\n', m->out);
    fflush(m->out);
}

void measurement_handle(Measurement *measurement, CGEventType type, CGEventRef event) {
    uint64_t now_ns = monotonic_now_ns();
    if (type == kCGEventScrollWheel) {
        handle_scroll(measurement, event, now_ns);
        return;
    }
    handle_button(measurement, type, event, now_ns);
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

    ThresholdAnalysis gap_analysis[MOUSE_BUTTON_COUNT];
    memset(gap_analysis, 0, sizeof(gap_analysis));

    TimingDraft suggested;
    timing_draft_init(&suggested);
    /* Preserve short-press settings; the existing heuristic estimates only hold-ms. */
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        timing_set_short0_button(&suggested, (MouseButton)button, m->timing.short0_ms[button]);
        timing_set_hold0_button(&suggested, (MouseButton)button, m->timing.hold0_ms[button]);
    }

    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        fprintf(m->out, "%s (%zu press, %zu release->down samples):\n",
            mouse_button_name((MouseButton)button),
            m->press_count[button], m->gap_count[button]);
        print_stats_line(m->out, "press durations", m->press_ms[button], m->press_count[button]);
        print_stats_line(m->out, "release->next-down gaps", m->gap_ms[button], m->gap_count[button]);

        gap_analysis[button] = stats_analyze_threshold(
            m->gap_ms[button], m->gap_count[button], ANALYSIS_WINDOW_MS);
        print_threshold_analysis(m->out, "hold-ms", gap_analysis[button]);

        if (gap_analysis[button].clear_split) {
            timing_set_hold_button(&suggested, (MouseButton)button,
                gap_analysis[button].suggested_threshold_ms);
        }
    }

    TimingSettings resolved;
    timing_resolve(&suggested, &resolved);

    fprintf(m->out,
        "\nSuggested settings\n------------------\n"
        "Heuristic only: separated low-timing clusters are analyzed after Tukey-IQR\n"
        "outlier removal. Missing hold values inherit measured siblings; otherwise %.0f ms.\n"
        "short0-ms and hold0-ms are kept as configured, not estimated.\n", DEFAULT_HOLD_MS);

    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        int hold_siblings = 0;
        for (int other = 0; other < MOUSE_BUTTON_COUNT; ++other) {
            if (other == button) continue;
            if (gap_analysis[other].clear_split) hold_siblings++;
        }
        fprintf(m->out, "  %-6s short0-ms=%.3g hold0-ms=%.3g (configured), hold-ms=%5.0f (%s)\n",
            mouse_button_name((MouseButton)button),
            resolved.short0_ms[button],
            resolved.hold0_ms[button],
            resolved.hold_ms[button],
            inheritance_label(gap_analysis[button].clear_split, hold_siblings));
    }

    fprintf(m->out, "\nSuggested config/CLI arguments:\n  ");
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        fprintf(m->out, "--%s-short0-ms %.3g --%s-hold0-ms %.3g --%s-hold-ms %.0f ",
            mouse_button_cli_name((MouseButton)button), resolved.short0_ms[button],
            mouse_button_cli_name((MouseButton)button), resolved.hold0_ms[button],
            mouse_button_cli_name((MouseButton)button), resolved.hold_ms[button]);
    }
    fprintf(m->out, "\n");

    size_t strong = m->wheel_vertical.strong_missing_events +
        m->wheel_horizontal.strong_missing_events;
    size_t weak = m->wheel_vertical.weak_missing_events +
        m->wheel_horizontal.weak_missing_events;
    fprintf(m->out,
        "\nWHEEL: %zu scroll events observed; %zu probable + %zu possible missing pulse(s)\n"
        "flagged in locally stable discrete-wheel runs. Possible = the enlarged gap also\n"
        "reset macOS scroll acceleration, which can mean either a new gesture or a real miss.\n"
        "These are diagnostics only; no wheel events are synthesized.\n",
        m->scroll_count, strong, weak);

    fprintf(m->out,
        "\nNext test suggestions\n---------------------\n"
        "- Try the suggested button settings with: mousedebouncectl save <args>\n"
        "- Then: mousedebouncectl restart\n"
        "- For wheel diagnosis, repeat: mousedebouncectl measure 60 and do >=1 s steady\n"
        "  one-direction scrolls. Lines marked <<< probable-*-miss or <<< possible-*-miss are candidates.\n"
        "- If the mouse was behaving perfectly, do not overfit settings to this session; rerun\n"
        "  when chatter/misses recur.\n");
    fflush(m->out);
}
