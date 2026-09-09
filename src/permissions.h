#ifndef MOUSE_DEBOUNCE_PERMISSIONS_H
#define MOUSE_DEBOUNCE_PERMISSIONS_H

#include <stdbool.h>

/*
 * MouseDebounce intentionally uses one active CoreGraphics event tap for both
 * filtering and measurement.  That keeps the app's TCC surface to a single
 * permission: Accessibility.  Measurement returns every observed event
 * unchanged.
 */
bool permissions_request_accessibility(void);
bool permissions_has_accessibility(void);

#endif
