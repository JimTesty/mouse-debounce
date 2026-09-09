/*
 * mac-mouse-debounce: tiny macOS mouse switch debounce filter + measurement tool.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Algorithm: based on franzos/mouse-debounce's measured two-threshold rule:
 *   - a release very soon after its press is suspicious and is withheld;
 *   - if another press arrives while that release is withheld, suppress both,
 *     so downstream sees one continuous press;
 *   - otherwise emit the withheld release after hold_ms.
 *
 * macOS event-tap/lifecycle approach: intentionally mirrors the small useful
 * subset of Vorssaint's MouseClickDebounceService: a HID-level head-insert
 * CGEventTap, fail-open handling of unmatched releases, and re-enabling a tap
 * disabled by timeout/user input.
 *
 * References:
 *   https://github.com/franzos/mouse-debounce
 *   https://github.com/vorssaint/vorssaint-utils
 *
 * This program has no networking, file I/O, subprocess execution, or updater.
 */

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUTTON_COUNT 3
#define MAX_SAMPLES 4096
#define DEFAULT_SHORT_MS 80.0
#define DEFAULT_HOLD_MS 70.0
#define SUMMARY_CUTOFF_MS 300.0
#define OWN_EVENT_MAGIC INT64_C(0x4d44424e43454f01) /* "MDBNCEO\1" */

typedef enum {
    MODE_NONE = 0,
    MODE_MEASURE,
    MODE_FILTER,
} Mode;

typedef struct {
    bool enabled;
    bool downstream_down;
    uint64_t last_physical_down_ns;
    CGEventRef pending_up;
    CFRunLoopTimerRef pending_timer;
} ButtonState;

typedef struct {
    uint64_t first_ns;
    uint64_t last_down_ns[BUTTON_COUNT];
    uint64_t last_up_ns[BUTTON_COUNT];
    double press_ms[BUTTON_COUNT][MAX_SAMPLES];
    double gap_ms[BUTTON_COUNT][MAX_SAMPLES];
    size_t press_count[BUTTON_COUNT];
    size_t gap_count[BUTTON_COUNT];
} MeasurementState;

static Mode g_mode = MODE_NONE;
static double g_short_ms = DEFAULT_SHORT_MS;
static double g_hold_ms = DEFAULT_HOLD_MS;
static ButtonState g_buttons[BUTTON_COUNT];
static MeasurementState g_measure;
static CFMachPortRef g_tap = NULL;
static CFRunLoopRef g_run_loop = NULL;
static int g_signal_pipe[2] = {-1, -1};
static CFFileDescriptorRef g_signal_fd = NULL;
static CFRunLoopSourceRef g_signal_source = NULL;


static uint64_t uptime_nanoseconds(void) {
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) (void)mach_timebase_info(&timebase);
    uint64_t ticks = mach_absolute_time();
    /* Use 128-bit intermediate so long uptimes cannot overflow the multiply. */
    __uint128_t ns = (__uint128_t)ticks * timebase.numer / timebase.denom;
    return (uint64_t)ns;
}

static const char *button_name(int button) {
    switch (button) {
        case 0: return "LEFT";
        case 1: return "RIGHT";
        case 2: return "MIDDLE";
        default: return "?";
    }
}

static int button_from_event(CGEventType type, CGEventRef event, bool *is_down, bool *is_up) {
    *is_down = false;
    *is_up = false;

    switch (type) {
        case kCGEventLeftMouseDown:  *is_down = true; return 0;
        case kCGEventLeftMouseUp:    *is_up = true;   return 0;
        case kCGEventRightMouseDown: *is_down = true; return 1;
        case kCGEventRightMouseUp:   *is_up = true;   return 1;
        case kCGEventOtherMouseDown:
        case kCGEventOtherMouseUp: {
            int64_t n = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (n != 2) return -1;
            if (type == kCGEventOtherMouseDown) *is_down = true;
            else *is_up = true;
            return 2;
        }
        default:
            return -1;
    }
}

static void cancel_pending_timer(ButtonState *state) {
    if (state->pending_timer != NULL) {
        CFRunLoopTimerInvalidate(state->pending_timer);
        CFRelease(state->pending_timer);
        state->pending_timer = NULL;
    }
}

static void post_owned_event(CGEventRef event) {
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, OWN_EVENT_MAGIC);
    CGEventPost(kCGHIDEventTap, event);
}

static void emit_pending_up(ButtonState *state) {
    if (state->pending_up == NULL) return;

    cancel_pending_timer(state);

    CGEventRef event = state->pending_up;
    state->pending_up = NULL;
    state->downstream_down = false;

    /* Use a fresh timestamp because this is the time downstream actually sees it. */
    CGEventSetTimestamp(event, uptime_nanoseconds());
    post_owned_event(event);
    CFRelease(event);
}

static void discard_pending_up(ButtonState *state) {
    cancel_pending_timer(state);
    if (state->pending_up != NULL) {
        CFRelease(state->pending_up);
        state->pending_up = NULL;
    }
}

static void pending_timer_callback(CFRunLoopTimerRef timer, void *info) {
    (void)timer;
    ButtonState *state = (ButtonState *)info;

    /* Clear our owning reference before emitting; callback is one-shot. */
    if (state->pending_timer != NULL) {
        CFRunLoopTimerRef owned = state->pending_timer;
        state->pending_timer = NULL;
        CFRunLoopTimerInvalidate(owned);
        CFRelease(owned);
    }

    if (state->pending_up != NULL) {
        CGEventRef event = state->pending_up;
        state->pending_up = NULL;
        state->downstream_down = false;
        CGEventSetTimestamp(event, uptime_nanoseconds());
        post_owned_event(event);
        CFRelease(event);
    }
}

static void schedule_pending_timer(ButtonState *state) {
    cancel_pending_timer(state);

    CFRunLoopTimerContext context = {0};
    context.info = state;
    CFAbsoluteTime fire = CFAbsoluteTimeGetCurrent() + g_hold_ms / 1000.0;
    state->pending_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        fire,
        0.0,              /* one-shot */
        0,
        0,
        pending_timer_callback,
        &context
    );
    if (state->pending_timer == NULL) {
        /* Fail open: if we cannot schedule the decision, release immediately. */
        emit_pending_up(state);
        return;
    }
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), state->pending_timer, kCFRunLoopCommonModes);
}

static void flush_all_pending(void) {
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        emit_pending_up(&g_buttons[i]);
    }
}

static void reset_filter_state_without_dropping_owed_ups(void) {
    flush_all_pending();
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        g_buttons[i].downstream_down = false;
        g_buttons[i].last_physical_down_ns = 0;
    }
}

static CGEventRef handle_filter_event(CGEventType type, CGEventRef event) {
    /* Never re-filter events that we synthesize to pay back a withheld Up. */
    if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == OWN_EVENT_MAGIC) {
        return event;
    }

    bool is_down = false, is_up = false;
    int button = button_from_event(type, event, &is_down, &is_up);
    if (button < 0 || !g_buttons[button].enabled) return event;

    ButtonState *state = &g_buttons[button];
    uint64_t now = CGEventGetTimestamp(event);

    if (is_down) {
        if (state->pending_up != NULL) {
            /*
             * Linux algorithm's key pair cancellation:
             * the withheld Up + this returning Down are the bounce pair.
             * Drop both, leaving downstream logically held continuously.
             */
            discard_pending_up(state);
            state->last_physical_down_ns = now;
            return NULL;
        }

        if (state->downstream_down) {
            /* Repeated Down with no intervening accepted Up: suppress fail-safe. */
            state->last_physical_down_ns = now;
            return NULL;
        }

        state->downstream_down = true;
        state->last_physical_down_ns = now;
        return event;
    }

    if (is_up) {
        if (!state->downstream_down) {
            /* Fail open: an unmatched Up can only help release a stuck button. */
            return event;
        }

        double held_ms = state->last_physical_down_ns == 0 || now < state->last_physical_down_ns
            ? 1e99
            : (double)(now - state->last_physical_down_ns) / 1e6;

        if (held_ms < g_short_ms) {
            discard_pending_up(state);
            state->pending_up = CGEventCreateCopy(event);
            if (state->pending_up == NULL) {
                /* Allocation failure: never eat an Up we cannot later restore. */
                state->downstream_down = false;
                return event;
            }
            schedule_pending_timer(state);
            return NULL;
        }

        state->downstream_down = false;
        return event;
    }

    return event;
}

static void add_sample(double samples[MAX_SAMPLES], size_t *count, double value) {
    if (*count < MAX_SAMPLES) samples[(*count)++] = value;
}

static void handle_measure_event(CGEventType type, CGEventRef event) {
    bool is_down = false, is_up = false;
    int button = button_from_event(type, event, &is_down, &is_up);
    if (button < 0 || !g_buttons[button].enabled) return;

    uint64_t now = CGEventGetTimestamp(event);
    if (g_measure.first_ns == 0) g_measure.first_ns = now;
    double elapsed_s = now >= g_measure.first_ns
        ? (double)(now - g_measure.first_ns) / 1e9
        : 0.0;
    int64_t click_state = CGEventGetIntegerValueField(event, kCGMouseEventClickState);

    if (is_down) {
        double gap_ms = -1.0;
        if (g_measure.last_up_ns[button] != 0 && now >= g_measure.last_up_ns[button]) {
            gap_ms = (double)(now - g_measure.last_up_ns[button]) / 1e6;
            add_sample(g_measure.gap_ms[button], &g_measure.gap_count[button], gap_ms);
        }
        g_measure.last_down_ns[button] = now;

        if (gap_ms >= 0.0)
            printf("%9.3fs  %-6s down   gap-from-up=%8.1f ms  clickState=%" PRId64 "\n",
                   elapsed_s, button_name(button), gap_ms, click_state);
        else
            printf("%9.3fs  %-6s down   gap-from-up=       -  clickState=%" PRId64 "\n",
                   elapsed_s, button_name(button), click_state);
    } else if (is_up) {
        double held_ms = -1.0;
        if (g_measure.last_down_ns[button] != 0 && now >= g_measure.last_down_ns[button]) {
            held_ms = (double)(now - g_measure.last_down_ns[button]) / 1e6;
            add_sample(g_measure.press_ms[button], &g_measure.press_count[button], held_ms);
        }
        g_measure.last_up_ns[button] = now;

        if (held_ms >= 0.0)
            printf("%9.3fs  %-6s up     press-held=%8.1f ms  clickState=%" PRId64 "\n",
                   elapsed_s, button_name(button), held_ms, click_state);
        else
            printf("%9.3fs  %-6s up     press-held=       -  clickState=%" PRId64 "\n",
                   elapsed_s, button_name(button), click_state);
    }
    fflush(stdout);
}

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void print_short_samples(const char *label, const double *samples, size_t count) {
    double temp[MAX_SAMPLES];
    size_t n = 0;
    for (size_t i = 0; i < count && n < MAX_SAMPLES; ++i) {
        if (samples[i] <= SUMMARY_CUTOFF_MS) temp[n++] = samples[i];
    }
    qsort(temp, n, sizeof(temp[0]), compare_double);

    printf("  %s <= %.0f ms (%zu):", label, SUMMARY_CUTOFF_MS, n);
    if (n == 0) {
        printf(" none\n");
        return;
    }
    for (size_t i = 0; i < n; ++i) printf(" %.1f", temp[i]);
    printf(" ms\n");
}

static void print_measurement_summary(void) {
    printf("\nMeasurement summary\n");
    printf("-------------------\n");
    for (int button = 0; button < BUTTON_COUNT; ++button) {
        if (!g_buttons[button].enabled) continue;
        printf("%s:\n", button_name(button));
        print_short_samples("press durations", g_measure.press_ms[button], g_measure.press_count[button]);
        print_short_samples("release->next-down gaps", g_measure.gap_ms[button], g_measure.gap_count[button]);
    }
    printf("\nChoose --short-ms above the longest bounce press-duration but below your\n"
           "shortest genuine click. Choose --hold-ms above the longest bounce\n"
           "release->returning-down gap but below the gap in deliberate fast\n"
           "double-clicks. Record soft clicks and drag-selects as well as normal clicks.\n");
}

static CGEventRef event_tap_callback(CGEventTapProxy proxy, CGEventType type,
                                     CGEventRef event, void *user_info) {
    (void)proxy;
    (void)user_info;

    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_mode == MODE_FILTER) reset_filter_state_without_dropping_owed_ups();
        if (g_tap != NULL) CGEventTapEnable(g_tap, true);
        return event;
    }

    if (g_mode == MODE_MEASURE) {
        handle_measure_event(type, event);
        return event; /* listen-only anyway */
    }

    return handle_filter_event(type, event);
}

static void signal_handler(int signo) {
    if (g_signal_pipe[1] < 0) return;
    unsigned char byte = (unsigned char)signo;
    (void)write(g_signal_pipe[1], &byte, 1); /* write() is async-signal-safe */
}

static void signal_fd_callback(CFFileDescriptorRef fdref, CFOptionFlags call_back_types,
                               void *info) {
    (void)fdref;
    (void)call_back_types;
    (void)info;

    unsigned char bytes[32];
    while (read(g_signal_pipe[0], bytes, sizeof(bytes)) > 0) {
        /* drain */
    }

    if (g_mode == MODE_FILTER) flush_all_pending();
    if (g_mode == MODE_MEASURE) print_measurement_summary();
    if (g_run_loop != NULL) CFRunLoopStop(g_run_loop);
}

static bool install_signal_bridge(void) {
    if (pipe(g_signal_pipe) != 0) return false;

    /* Nonblocking read end lets the run-loop callback drain without hanging. */
    int flags = fcntl(g_signal_pipe[0], F_GETFL, 0);
    if (flags >= 0) (void)fcntl(g_signal_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(g_signal_pipe[1], F_GETFL, 0);
    if (flags >= 0) (void)fcntl(g_signal_pipe[1], F_SETFL, flags | O_NONBLOCK);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        return false;
    }

    CFFileDescriptorContext context = {0};
    g_signal_fd = CFFileDescriptorCreate(kCFAllocatorDefault, g_signal_pipe[0], false,
                                         signal_fd_callback, &context);
    if (g_signal_fd == NULL) return false;

    g_signal_source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, g_signal_fd, 0);
    if (g_signal_source == NULL) return false;

    CFRunLoopAddSource(CFRunLoopGetCurrent(), g_signal_source, kCFRunLoopCommonModes);
    CFFileDescriptorEnableCallBacks(g_signal_fd, kCFFileDescriptorReadCallBack);
    return true;
}

static void cleanup(void) {
    if (g_mode == MODE_FILTER) flush_all_pending();

    if (g_tap != NULL) {
        CGEventTapEnable(g_tap, false);
        CFMachPortInvalidate(g_tap);
        CFRelease(g_tap);
        g_tap = NULL;
    }
    if (g_signal_source != NULL) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), g_signal_source, kCFRunLoopCommonModes);
        CFRelease(g_signal_source);
        g_signal_source = NULL;
    }
    if (g_signal_fd != NULL) {
        CFFileDescriptorInvalidate(g_signal_fd);
        CFRelease(g_signal_fd);
        g_signal_fd = NULL;
    }
    if (g_signal_pipe[0] >= 0) close(g_signal_pipe[0]);
    if (g_signal_pipe[1] >= 0) close(g_signal_pipe[1]);
}

static bool parse_buttons(const char *text) {
    for (int i = 0; i < BUTTON_COUNT; ++i) g_buttons[i].enabled = false;

    char *copy = strdup(text);
    if (copy == NULL) return false;
    bool any = false;

    for (char *tok = strtok(copy, ","); tok != NULL; tok = strtok(NULL, ",")) {
        int button = -1;
        if (strcmp(tok, "left") == 0) button = 0;
        else if (strcmp(tok, "right") == 0) button = 1;
        else if (strcmp(tok, "middle") == 0) button = 2;
        else {
            free(copy);
            return false;
        }
        g_buttons[button].enabled = true;
        any = true;
    }
    free(copy);
    return any;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --measure [--buttons left[,right,middle]]\n"
        "  %s --filter  [--short-ms N] [--hold-ms N] [--buttons ...]\n"
        "\n"
        "Modes:\n"
        "  --measure       listen only; print raw Down/Up timing and summary on Ctrl-C\n"
        "  --filter        actively suppress switch bounce\n"
        "\n"
        "Filter defaults:\n"
        "  --short-ms %.0f   releases sooner than this after Down are suspicious\n"
        "  --hold-ms  %.0f   withhold suspicious Up this long awaiting returning Down\n"
        "  --buttons left    debounce/measure left button only\n"
        "\n"
        "Example:\n"
        "  %s --measure\n"
        "  %s --filter --short-ms 80 --hold-ms 70\n",
        argv0, argv0, DEFAULT_SHORT_MS, DEFAULT_HOLD_MS, argv0, argv0);
}

int main(int argc, char **argv) {
    const char *button_list = "left";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--measure") == 0) {
            if (g_mode != MODE_NONE) { usage(argv[0]); return 2; }
            g_mode = MODE_MEASURE;
        } else if (strcmp(argv[i], "--filter") == 0) {
            if (g_mode != MODE_NONE) { usage(argv[0]); return 2; }
            g_mode = MODE_FILTER;
        } else if (strcmp(argv[i], "--short-ms") == 0 && i + 1 < argc) {
            g_short_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--hold-ms") == 0 && i + 1 < argc) {
            g_hold_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--buttons") == 0 && i + 1 < argc) {
            button_list = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (g_mode == MODE_NONE || g_short_ms <= 0.0 || g_hold_ms <= 0.0 || !parse_buttons(button_list)) {
        usage(argv[0]);
        return 2;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    g_run_loop = CFRunLoopGetCurrent();

    CGEventMask mask =
        CGEventMaskBit(kCGEventLeftMouseDown) |
        CGEventMaskBit(kCGEventLeftMouseUp) |
        CGEventMaskBit(kCGEventRightMouseDown) |
        CGEventMaskBit(kCGEventRightMouseUp) |
        CGEventMaskBit(kCGEventOtherMouseDown) |
        CGEventMaskBit(kCGEventOtherMouseUp);

    CGEventTapOptions options = g_mode == MODE_MEASURE
        ? kCGEventTapOptionListenOnly
        : kCGEventTapOptionDefault;

    g_tap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, options,
                             mask, event_tap_callback, NULL);
    if (g_tap == NULL) {
        fprintf(stderr,
            "Could not create CGEventTap. macOS probably denied event access.\n"
            "Grant this executable (or the Terminal app launching it) the relevant\n"
            "Privacy & Security permission, typically Accessibility/Input Monitoring,\n"
            "then run it again.\n");
        return 1;
    }

    CFRunLoopSourceRef tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_tap, 0);
    if (tap_source == NULL) {
        fprintf(stderr, "Could not create run-loop source for event tap.\n");
        cleanup();
        return 1;
    }
    CFRunLoopAddSource(g_run_loop, tap_source, kCFRunLoopCommonModes);
    CGEventTapEnable(g_tap, true);

    if (!install_signal_bridge()) {
        fprintf(stderr, "Warning: failed to install clean SIGINT/SIGTERM bridge.\n");
    }

    if (g_mode == MODE_MEASURE) {
        printf("Measuring CGHIDEventTap mouse transitions; events are NOT modified.\n"
               "Click normally, include soft clicks and drag-selects, then Ctrl-C.\n\n");
    } else {
        printf("Filtering %s: short-ms=%.1f hold-ms=%.1f. Ctrl-C to stop.\n",
               button_list, g_short_ms, g_hold_ms);
    }

    CFRunLoopRun();

    CFRunLoopRemoveSource(g_run_loop, tap_source, kCFRunLoopCommonModes);
    CFRelease(tap_source);
    cleanup();
    return 0;
}
