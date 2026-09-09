#include "signal_bridge.h"

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static SignalBridge *g_bridge = NULL;

static void posix_signal_handler(int signo) {
    (void)signo;
    if (g_bridge == NULL || g_bridge->pipe_fd[1] < 0) return;
    unsigned char byte = 1;
    (void)write(g_bridge->pipe_fd[1], &byte, 1);
}

static void fd_callback(
    CFFileDescriptorRef fdref,
    CFOptionFlags call_back_types,
    void *info
) {
    (void)fdref;
    (void)call_back_types;
    SignalBridge *bridge = (SignalBridge *)info;

    unsigned char bytes[32];
    while (read(bridge->pipe_fd[0], bytes, sizeof(bytes)) > 0) {
        /* drain */
    }

    if (bridge->callback != NULL) bridge->callback(bridge->context);
}

bool signal_bridge_start(SignalBridge *bridge, SignalBridgeCallback callback, void *context) {
    memset(bridge, 0, sizeof(*bridge));
    bridge->pipe_fd[0] = -1;
    bridge->pipe_fd[1] = -1;
    bridge->callback = callback;
    bridge->context = context;

    if (pipe(bridge->pipe_fd) != 0) return false;

    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(bridge->pipe_fd[i], F_GETFL, 0);
        if (flags >= 0) (void)fcntl(bridge->pipe_fd[i], F_SETFL, flags | O_NONBLOCK);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        return false;
    }

    CFFileDescriptorContext fd_context = {0};
    fd_context.info = bridge;
    bridge->descriptor = CFFileDescriptorCreate(
        kCFAllocatorDefault, bridge->pipe_fd[0], false, fd_callback, &fd_context
    );
    if (bridge->descriptor == NULL) return false;

    bridge->source = CFFileDescriptorCreateRunLoopSource(
        kCFAllocatorDefault, bridge->descriptor, 0
    );
    if (bridge->source == NULL) return false;

    CFRunLoopAddSource(CFRunLoopGetCurrent(), bridge->source, kCFRunLoopCommonModes);
    CFFileDescriptorEnableCallBacks(bridge->descriptor, kCFFileDescriptorReadCallBack);
    g_bridge = bridge;
    return true;
}

void signal_bridge_stop(SignalBridge *bridge) {
    if (g_bridge == bridge) g_bridge = NULL;

    if (bridge->source != NULL) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), bridge->source, kCFRunLoopCommonModes);
        CFRelease(bridge->source);
        bridge->source = NULL;
    }
    if (bridge->descriptor != NULL) {
        CFFileDescriptorInvalidate(bridge->descriptor);
        CFRelease(bridge->descriptor);
        bridge->descriptor = NULL;
    }
    if (bridge->pipe_fd[0] >= 0) close(bridge->pipe_fd[0]);
    if (bridge->pipe_fd[1] >= 0) close(bridge->pipe_fd[1]);
    bridge->pipe_fd[0] = bridge->pipe_fd[1] = -1;
}
