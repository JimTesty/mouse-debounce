#include "mouse_button.h"

const char *mouse_button_name(MouseButton button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return "LEFT";
        case MOUSE_BUTTON_RIGHT: return "RIGHT";
        case MOUSE_BUTTON_MIDDLE: return "MIDDLE";
        default: return "?";
    }
}

const char *mouse_button_cli_name(MouseButton button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return "left";
        case MOUSE_BUTTON_RIGHT: return "right";
        case MOUSE_BUTTON_MIDDLE: return "middle";
        default: return "?";
    }
}
