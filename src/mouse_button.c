#include "mouse_button.h"

#include <string.h>

bool mouse_parse_button_list(const char *text, bool enabled[MOUSE_BUTTON_COUNT]) {
    bool parsed[MOUSE_BUTTON_COUNT] = {false};
    const char *start = text;
    do {
        const char *end = strchr(start, ',');
        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
        int button;
        for (button = 0; button < MOUSE_BUTTON_COUNT; ++button) {
            const char *name = mouse_button_cli_name((MouseButton)button);
            if (strlen(name) == length && strncmp(start, name, length) == 0) break;
        }
        if (button == MOUSE_BUTTON_COUNT) return false;
        parsed[button] = true;
        start = end != NULL ? end + 1 : NULL;
    } while (start != NULL);
    memcpy(enabled, parsed, sizeof(parsed));
    return true;
}

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
