#include "debounce_filter.h"

#include <inttypes.h>
#include <mach/mach_time.h>
#include <string.h>

#define OWN_EVENT_MAGIC INT64_C(0x4d44424e43454f02)

typedef struct {
    DebounceFilter *filter;
    MouseButton button;
} TimerContext;

static TimerContext g_timer_contexts[MOUSE_BUTTON_COUNT];

static uint64_t uptime_nanoseconds(void) {
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) (void)mach_timebase_info(&timebase);
    __uint128_t ns = (__uint128_t)mach_absolute_time() * timebase.numer / timebase.denom;
    return (uint64_t)ns;
}

static uint64_t ms_to_ns(double ms) {
    if (ms <= 0.0) return 0;
    return (uint64_t)(ms * 1000000.0 + 0.5);
}

static void cancel_timer(DebounceButtonRuntime *runtime) {
    if (runtime->pending_timer == NULL) return;
    CFRunLoopTimerInvalidate(runtime->pending_timer);
    CFRelease(runtime->pending_timer);
    runtime->pending_timer = NULL;
}

static void post_owned_up(DebounceButtonRuntime *runtime) {
    if (runtime->pending_up == NULL) return;

    cancel_timer(runtime);
    CGEventRef event = runtime->pending_up;
    runtime->pending_up = NULL;
    debounce_pending_emitted(&runtime->logic);

    CGEventSetTimestamp(event, uptime_nanoseconds());
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, OWN_EVENT_MAGIC);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void discard_pending_up(DebounceButtonRuntime *runtime) {
    cancel_timer(runtime);
    if (runtime->pending_up != NULL) {
        CFRelease(runtime->pending_up);
        runtime->pending_up = NULL;
    }
}

static void timer_callback(CFRunLoopTimerRef timer, void *info) {
    (void)timer;
    TimerContext *ctx = (TimerContext *)info;
    DebounceButtonRuntime *runtime = &ctx->filter->button[ctx->button];

    if (runtime->pending_timer != NULL) {
        CFRunLoopTimerRef owned = runtime->pending_timer;
        runtime->pending_timer = NULL;
        CFRunLoopTimerInvalidate(owned);
        CFRelease(owned);
    }

    if (runtime->pending_up != NULL) {
        CGEventRef event = runtime->pending_up;
        runtime->pending_up = NULL;
        debounce_pending_emitted(&runtime->logic);
        CGEventSetTimestamp(event, uptime_nanoseconds());
        CGEventSetIntegerValueField(event, kCGEventSourceUserData, OWN_EVENT_MAGIC);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
}

static bool schedule_timer(DebounceFilter *filter, MouseButton button) {
    DebounceButtonRuntime *runtime = &filter->button[button];
    cancel_timer(runtime);

    g_timer_contexts[button].filter = filter;
    g_timer_contexts[button].button = button;

    CFRunLoopTimerContext context = {0};
    context.info = &g_timer_contexts[button];
    CFAbsoluteTime fire = CFAbsoluteTimeGetCurrent() + (double)filter->hold_ns[button] / 1e9;
    runtime->pending_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault, fire, 0.0, 0, 0, timer_callback, &context
    );
    if (runtime->pending_timer == NULL) return false;

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), runtime->pending_timer, kCFRunLoopCommonModes);
    return true;
}

void debounce_filter_init(
    DebounceFilter *filter,
    const bool enabled[MOUSE_BUTTON_COUNT],
    const double short_ms[MOUSE_BUTTON_COUNT],
    const double hold_ms[MOUSE_BUTTON_COUNT]
) {
    memset(filter, 0, sizeof(*filter));
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        filter->enabled[i] = enabled[i];
        filter->short_ns[i] = ms_to_ns(short_ms[i]);
        filter->hold_ns[i] = ms_to_ns(hold_ms[i]);
        debounce_state_init(&filter->button[i].logic);
    }
}

CGEventRef debounce_filter_handle(
    DebounceFilter *filter,
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event
) {
    (void)proxy;

    if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == OWN_EVENT_MAGIC) {
        return event;
    }

    MouseButtonEvent mouse;
    if (!mouse_button_from_event(type, event, &mouse) || !filter->enabled[mouse.button]) {
        return event;
    }

    DebounceButtonRuntime *runtime = &filter->button[mouse.button];
    uint64_t now_ns = CGEventGetTimestamp(event);

    if (mouse.is_down) {
        DebounceAction action = debounce_on_down(&runtime->logic, now_ns);

        if (action == DEBOUNCE_EXPIRE_PENDING_AND_RETRY_DOWN) {
            post_owned_up(runtime);
            action = debounce_on_down(&runtime->logic, now_ns);
        }

        if (action == DEBOUNCE_CANCEL_PENDING_AND_DROP_DOWN) {
            discard_pending_up(runtime);
            return NULL;
        }
        if (action == DEBOUNCE_DROP) return NULL;
        return event;
    }

    DebounceAction action = debounce_on_up(
        &runtime->logic, now_ns, filter->short_ns[mouse.button], filter->hold_ns[mouse.button]
    );
    if (action != DEBOUNCE_HOLD_UP) return event;

    discard_pending_up(runtime);
    runtime->pending_up = CGEventCreateCopy(event);
    if (runtime->pending_up == NULL) {
        /* Allocation failure: never eat an Up that we cannot later restore. */
        debounce_pending_emitted(&runtime->logic);
        return event;
    }

    if (!schedule_timer(filter, mouse.button)) {
        /* Timer failure: fail open immediately. */
        post_owned_up(runtime);
    }
    return NULL;
}

void debounce_filter_flush(DebounceFilter *filter) {
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        post_owned_up(&filter->button[i]);
    }
}

void debounce_filter_reset_safely(DebounceFilter *filter) {
    debounce_filter_flush(filter);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        debounce_state_init(&filter->button[i].logic);
    }
}

void debounce_filter_destroy(DebounceFilter *filter) {
    debounce_filter_flush(filter);
    for (int i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        discard_pending_up(&filter->button[i]);
    }
}
