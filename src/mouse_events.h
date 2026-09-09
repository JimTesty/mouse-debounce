#ifndef MOUSE_DEBOUNCE_MOUSE_EVENTS_H
#define MOUSE_DEBOUNCE_MOUSE_EVENTS_H

#include "mouse_button.h"

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>

typedef struct {
    bool is_down;
    bool is_up;
    MouseButton button;
} MouseButtonEvent;

bool mouse_button_from_event(CGEventType type, CGEventRef event, MouseButtonEvent *out);
bool mouse_parse_button_list(const char *text, bool enabled[MOUSE_BUTTON_COUNT]);
CGEventMask mouse_button_event_mask(void);
CGEventTimestamp mouse_current_event_timestamp(void);

#endif
