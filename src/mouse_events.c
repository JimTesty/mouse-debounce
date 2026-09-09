#include "mouse_events.h"

#include <stdlib.h>
#include <string.h>

const char *mouse_button_name(MouseButton button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return "LEFT";
        case MOUSE_BUTTON_RIGHT: return "RIGHT";
        case MOUSE_BUTTON_MIDDLE: return "MIDDLE";
        default: return "?";
    }
}

bool mouse_button_from_event(CGEventType type, CGEventRef event, MouseButtonEvent *out) {
    out->is_down = false;
    out->is_up = false;

    switch (type) {
        case kCGEventLeftMouseDown:
            out->is_down = true;
            out->button = MOUSE_BUTTON_LEFT;
            return true;
        case kCGEventLeftMouseUp:
            out->is_up = true;
            out->button = MOUSE_BUTTON_LEFT;
            return true;
        case kCGEventRightMouseDown:
            out->is_down = true;
            out->button = MOUSE_BUTTON_RIGHT;
            return true;
        case kCGEventRightMouseUp:
            out->is_up = true;
            out->button = MOUSE_BUTTON_RIGHT;
            return true;
        case kCGEventOtherMouseDown:
        case kCGEventOtherMouseUp: {
            int64_t n = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (n != 2) return false;
            out->button = MOUSE_BUTTON_MIDDLE;
            out->is_down = type == kCGEventOtherMouseDown;
            out->is_up = !out->is_down;
            return true;
        }
        default:
            return false;
    }
}

bool mouse_parse_button_list(const char *text, bool enabled[MOUSE_BUTTON_COUNT]) {
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) enabled[i] = false;

    char *copy = strdup(text);
    if (copy == NULL) return false;

    bool any = false;
    for (char *tok = strtok(copy, ","); tok != NULL; tok = strtok(NULL, ",")) {
        MouseButton button;
        if (strcmp(tok, "left") == 0) button = MOUSE_BUTTON_LEFT;
        else if (strcmp(tok, "right") == 0) button = MOUSE_BUTTON_RIGHT;
        else if (strcmp(tok, "middle") == 0) button = MOUSE_BUTTON_MIDDLE;
        else {
            free(copy);
            return false;
        }
        enabled[button] = true;
        any = true;
    }

    free(copy);
    return any;
}

CGEventMask mouse_button_event_mask(void) {
    return CGEventMaskBit(kCGEventLeftMouseDown) |
           CGEventMaskBit(kCGEventLeftMouseUp) |
           CGEventMaskBit(kCGEventRightMouseDown) |
           CGEventMaskBit(kCGEventRightMouseUp) |
           CGEventMaskBit(kCGEventOtherMouseDown) |
           CGEventMaskBit(kCGEventOtherMouseUp);
}
