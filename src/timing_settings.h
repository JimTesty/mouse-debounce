#ifndef MOUSE_DEBOUNCE_TIMING_SETTINGS_H
#define MOUSE_DEBOUNCE_TIMING_SETTINGS_H

#include "mouse_button.h"

#include <stdbool.h>

#define DEFAULT_SHORT_MS 20.0
#define DEFAULT_HOLD_MS 20.0

typedef struct {
    bool short_set[MOUSE_BUTTON_COUNT];
    bool hold_set[MOUSE_BUTTON_COUNT];
    double short_ms[MOUSE_BUTTON_COUNT];
    double hold_ms[MOUSE_BUTTON_COUNT];
} TimingDraft;

typedef struct {
    double short_ms[MOUSE_BUTTON_COUNT];
    double hold_ms[MOUSE_BUTTON_COUNT];
} TimingSettings;

void timing_draft_init(TimingDraft *draft);
void timing_set_short_all(TimingDraft *draft, double value);
void timing_set_hold_all(TimingDraft *draft, double value);
void timing_set_short_button(TimingDraft *draft, MouseButton button, double value);
void timing_set_hold_button(TimingDraft *draft, MouseButton button, double value);
void timing_resolve(const TimingDraft *draft, TimingSettings *settings);

#endif
