#include "signal_bridge.h"

#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/* POSIX signal handlers are process-wide, so only one bridge may own them. */
static SignalBridge *g_bridge = NULL;
static volatile sig_atomic_t g_write_fd = -1;

static void posix_signal_handler(int signo) {
    (void)signo;
    int saved_errno = errno;
    int fd = g_write_fd;
    if (fd < 0) return;
    unsigned char byte = 1;
    (void)write(fd, &byte, 1);
    errno = saved_errno;
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
    if (g_bridge != NULL) return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->pipe_fd[0] = -1;
    bridge->pipe_fd[1] = -1;
    bridge->callback = callback;
    bridge->context = context;

    if (pipe(bridge->pipe_fd) != 0) return false;

    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(bridge->pipe_fd[i], F_GETFL, 0);
        if (flags < 0 || fcntl(bridge->pipe_fd[i], F_SETFL, flags | O_NONBLOCK) < 0) goto fail;
    }

    CFFileDescriptorContext fd_context = {0};
    fd_context.info = bridge;
    bridge->descriptor = CFFileDescriptorCreate(
        kCFAllocatorDefault, bridge->pipe_fd[0], false, fd_callback, &fd_context
    );
    if (bridge->descriptor == NULL) goto fail;

    bridge->source = CFFileDescriptorCreateRunLoopSource(
        kCFAllocatorDefault, bridge->descriptor, 0
    );
    if (bridge->source == NULL) goto fail;

    CFRunLoopAddSource(CFRunLoopGetCurrent(), bridge->source, kCFRunLoopCommonModes);
    CFFileDescriptorEnableCallBacks(bridge->descriptor, kCFFileDescriptorReadCallBack);
    g_bridge = bridge;
    g_write_fd = bridge->pipe_fd[1];

    /* Install handlers only after the pipe/run-loop path is ready. Roll back
       even a partially installed pair so failed startup cannot swallow signals. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, &bridge->previous_int) != 0) goto fail;
    bridge->int_installed = true;
    if (sigaction(SIGTERM, &sa, &bridge->previous_term) != 0) goto fail;
    bridge->term_installed = true;
    return true;

fail:
    signal_bridge_stop(bridge);
    return false;
}

void signal_bridge_stop(SignalBridge *bridge) {
    if (g_bridge == bridge) {
        g_write_fd = -1;
        g_bridge = NULL;
    }
    if (bridge->int_installed) sigaction(SIGINT, &bridge->previous_int, NULL);
    if (bridge->term_installed) sigaction(SIGTERM, &bridge->previous_term, NULL);
    bridge->int_installed = bridge->term_installed = false;

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
