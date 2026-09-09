#ifndef MOUSE_DEBOUNCE_MOUSE_EVENTS_H
#define MOUSE_DEBOUNCE_MOUSE_EVENTS_H

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>

typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT = 1,
    MOUSE_BUTTON_MIDDLE = 2,
    MOUSE_BUTTON_COUNT = 3,
} MouseButton;

typedef struct {
    bool is_down;
    bool is_up;
    MouseButton button;
} MouseButtonEvent;

const char *mouse_button_name(MouseButton button);
bool mouse_button_from_event(CGEventType type, CGEventRef event, MouseButtonEvent *out);
bool mouse_parse_button_list(const char *text, bool enabled[MOUSE_BUTTON_COUNT]);
CGEventMask mouse_button_event_mask(void);

#endif
