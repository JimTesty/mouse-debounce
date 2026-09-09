#include "measurement.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define SUMMARY_CUTOFF_MS 300.0

static void add_sample(double *samples, size_t *count, double value) {
    if (*count < MEASUREMENT_MAX_SAMPLES) samples[(*count)++] = value;
}

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void print_short_samples(
    FILE *out,
    const char *label,
    const double *samples,
    size_t count
) {
    double temp[MEASUREMENT_MAX_SAMPLES];
    size_t n = 0;
    for (size_t i = 0; i < count && n < MEASUREMENT_MAX_SAMPLES; ++i) {
        if (samples[i] <= SUMMARY_CUTOFF_MS) temp[n++] = samples[i];
    }
    qsort(temp, n, sizeof(temp[0]), compare_double);

    fprintf(out, "  %s <= %.0f ms (%zu):", label, SUMMARY_CUTOFF_MS, n);
    if (n == 0) {
        fprintf(out, " none\n");
        return;
    }
    for (size_t i = 0; i < n; ++i) fprintf(out, " %.1f", temp[i]);
    fprintf(out, " ms\n");
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

void measurement_print_summary(Measurement *m) {
    fprintf(m->out, "\nMeasurement summary\n-------------------\n");
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (!m->enabled[button]) continue;
        fprintf(m->out, "%s:\n", mouse_button_name((MouseButton)button));
        print_short_samples(m->out, "press durations", m->press_ms[button], m->press_count[button]);
        print_short_samples(m->out, "release->next-down gaps", m->gap_ms[button], m->gap_count[button]);
    }
    fprintf(m->out,
        "WHEEL: %zu scroll events observed. A completely missing wheel event leaves no\n"
        "observable evidence, so this tool does not invent replacement wheel events.\n"
        "The wheel trace can still reveal isolated reverse ticks or erratic deltas.\n\n",
        m->scroll_count);
    fprintf(m->out,
        "Choose --short-ms above bounce press durations but below genuine fast clicks;\n"
        "choose --hold-ms above bounce release->returning-down gaps but below deliberate\n"
        "double-click gaps. Include soft clicks and drag-selects in the measurement.\n");
    fflush(m->out);
}
