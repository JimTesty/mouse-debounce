#ifndef MOUSE_DEBOUNCE_EVENT_TAP_H
#define MOUSE_DEBOUNCE_EVENT_TAP_H

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>

typedef CGEventRef (*EventTapHandler)(
    void *context,
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event
);

typedef void (*EventTapResetHandler)(void *context);

typedef struct {
    CFMachPortRef tap;
    CFRunLoopSourceRef source;
    EventTapHandler handler;
    EventTapResetHandler reset_handler;
    void *context;
} EventTap;

bool event_tap_start(
    EventTap *tap,
    bool listen_only,
    CGEventMask mask,
    EventTapHandler handler,
    EventTapResetHandler reset_handler,
    void *context
);
void event_tap_stop(EventTap *tap);

#endif
