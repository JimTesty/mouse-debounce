#include "timing_settings.h"

#include <string.h>

void timing_draft_init(TimingDraft *draft) {
    memset(draft, 0, sizeof(*draft));
}

void timing_set_short0_all(TimingDraft *draft, double value) {
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        draft->short0_set[i] = true;
        draft->short0_ms[i] = value;
    }
}

void timing_set_hold0_all(TimingDraft *draft, double value) {
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        draft->hold0_set[i] = true;
        draft->hold0_ms[i] = value;
    }
}

void timing_set_hold_all(TimingDraft *draft, double value) {
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        draft->hold_set[i] = true;
        draft->hold_ms[i] = value;
    }
}

void timing_set_short0_button(TimingDraft *draft, MouseButton button, double value) {
    draft->short0_set[button] = true;
    draft->short0_ms[button] = value;
}

void timing_set_hold0_button(TimingDraft *draft, MouseButton button, double value) {
    draft->hold0_set[button] = true;
    draft->hold0_ms[button] = value;
}

void timing_set_hold_button(TimingDraft *draft, MouseButton button, double value) {
    draft->hold_set[button] = true;
    draft->hold_ms[button] = value;
}

static void resolve_metric(
    const bool set[MOUSE_BUTTON_COUNT],
    const double value[MOUSE_BUTTON_COUNT],
    double fallback,
    double out[MOUSE_BUTTON_COUNT]
) {
    for (int button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
        if (set[button]) {
            out[button] = value[button];
            continue;
        }

        double sum = 0.0;
        int siblings = 0;
        for (int other = 0; other < MOUSE_BUTTON_COUNT; ++other) {
            if (other == button || !set[other]) continue;
            sum += value[other];
            siblings++;
        }
        out[button] = siblings > 0 ? sum / siblings : fallback;
    }
}

void timing_resolve(const TimingDraft *draft, TimingSettings *settings) {
    resolve_metric(draft->short0_set, draft->short0_ms, DEFAULT_SHORT0_MS, settings->short0_ms);
    resolve_metric(draft->hold0_set, draft->hold0_ms, DEFAULT_HOLD0_MS, settings->hold0_ms);
    resolve_metric(draft->hold_set, draft->hold_ms, DEFAULT_HOLD_MS, settings->hold_ms);
}
