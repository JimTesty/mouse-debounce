#ifndef MOUSE_DEBOUNCE_SIGNAL_BRIDGE_H
#define MOUSE_DEBOUNCE_SIGNAL_BRIDGE_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <signal.h>

typedef void (*SignalBridgeCallback)(void *context);

typedef struct {
    int pipe_fd[2];
    CFFileDescriptorRef descriptor;
    CFRunLoopSourceRef source;
    SignalBridgeCallback callback;
    void *context;
    struct sigaction previous_int;
    struct sigaction previous_term;
    bool int_installed;
    bool term_installed;
} SignalBridge;

bool signal_bridge_start(SignalBridge *bridge, SignalBridgeCallback callback, void *context);
void signal_bridge_stop(SignalBridge *bridge);

#endif
