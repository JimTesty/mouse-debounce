#include "event_tap.h"

#include <string.h>

static CGEventRef tap_callback(
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event,
    void *user_info
) {
    EventTap *tap = (EventTap *)user_info;

    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (tap->reset_handler != NULL) tap->reset_handler(tap->context);
        if (tap->tap != NULL) CGEventTapEnable(tap->tap, true);
        return event;
    }

    return tap->handler != NULL
        ? tap->handler(tap->context, proxy, type, event)
        : event;
}

bool event_tap_start(
    EventTap *tap,
    bool listen_only,
    CGEventMask mask,
    EventTapHandler handler,
    EventTapResetHandler reset_handler,
    void *context
) {
    memset(tap, 0, sizeof(*tap));
    tap->handler = handler;
    tap->reset_handler = reset_handler;
    tap->context = context;

    CGEventTapOptions options = listen_only ? kCGEventTapOptionListenOnly : kCGEventTapOptionDefault;
    tap->tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        options,
        mask,
        tap_callback,
        tap
    );
    if (tap->tap == NULL) return false;

    tap->source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap->tap, 0);
    if (tap->source == NULL) {
        CFMachPortInvalidate(tap->tap);
        CFRelease(tap->tap);
        tap->tap = NULL;
        return false;
    }

    CFRunLoopAddSource(CFRunLoopGetCurrent(), tap->source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap->tap, true);
    return true;
}

void event_tap_stop(EventTap *tap) {
    if (tap->source != NULL) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), tap->source, kCFRunLoopCommonModes);
        CFRelease(tap->source);
        tap->source = NULL;
    }
    if (tap->tap != NULL) {
        CGEventTapEnable(tap->tap, false);
        CFMachPortInvalidate(tap->tap);
        CFRelease(tap->tap);
        tap->tap = NULL;
    }
}
