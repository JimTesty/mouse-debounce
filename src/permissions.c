#include "permissions.h"

#include <ApplicationServices/ApplicationServices.h>

bool permissions_has_accessibility(void) {
    return AXIsProcessTrusted();
}

bool permissions_request_accessibility(void) {
    if (permissions_has_accessibility()) return true;

    /*
     * AXIsProcessTrustedWithOptions is used here instead of requesting
     * listen-event access.  kAXTrustedCheckOptionPrompt reliably associates
     * the request with Accessibility in System Settings.  The function can
     * still return false on this invocation while macOS presents the UI;
     * callers should exit cleanly and be relaunched after the user grants it.
     */
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    if (options != NULL) {
        (void)AXIsProcessTrustedWithOptions(options);
        CFRelease(options);
    }

    return permissions_has_accessibility();
}
