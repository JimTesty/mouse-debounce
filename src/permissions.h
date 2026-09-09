#ifndef MOUSE_DEBOUNCE_PERMISSIONS_H
#define MOUSE_DEBOUNCE_PERMISSIONS_H

#include <stdbool.h>

typedef struct {
    bool listen_allowed;
    bool post_allowed;
} PermissionStatus;

PermissionStatus permissions_request(bool needs_post_access);

#endif
