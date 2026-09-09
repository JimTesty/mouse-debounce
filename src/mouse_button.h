#ifndef MOUSE_DEBOUNCE_MOUSE_BUTTON_H
#define MOUSE_DEBOUNCE_MOUSE_BUTTON_H

typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT = 1,
    MOUSE_BUTTON_MIDDLE = 2,
    MOUSE_BUTTON_COUNT = 3,
} MouseButton;

const char *mouse_button_name(MouseButton button);
const char *mouse_button_cli_name(MouseButton button);

#endif
