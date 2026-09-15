#include "mouse_events.h"

bool mouse_button_from_event(CGEventType type, CGEventRef event, MouseButtonEvent *out) {
    out->is_down = false;

    switch (type) {
        case kCGEventLeftMouseDown:
            out->is_down = true;
            out->button = MOUSE_BUTTON_LEFT;
            return true;
        case kCGEventLeftMouseUp:
            out->button = MOUSE_BUTTON_LEFT;
            return true;
        case kCGEventRightMouseDown:
            out->is_down = true;
            out->button = MOUSE_BUTTON_RIGHT;
            return true;
        case kCGEventRightMouseUp:
            out->button = MOUSE_BUTTON_RIGHT;
            return true;
        case kCGEventOtherMouseDown:
        case kCGEventOtherMouseUp: {
            int64_t n = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (n != 2) return false;
            out->button = MOUSE_BUTTON_MIDDLE;
            out->is_down = type == kCGEventOtherMouseDown;
            return true;
        }
        default:
            return false;
    }
}

CGEventMask mouse_button_event_mask(void) {
    return CGEventMaskBit(kCGEventLeftMouseDown) |
           CGEventMaskBit(kCGEventLeftMouseUp) |
           CGEventMaskBit(kCGEventRightMouseDown) |
           CGEventMaskBit(kCGEventRightMouseUp) |
           CGEventMaskBit(kCGEventOtherMouseDown) |
           CGEventMaskBit(kCGEventOtherMouseUp);
}
