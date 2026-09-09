#include "permissions.h"

#include <ApplicationServices/ApplicationServices.h>

PermissionStatus permissions_request(bool needs_post_access) {
    PermissionStatus status = {0};

    status.listen_allowed = CGPreflightListenEventAccess();
    if (!status.listen_allowed) {
        status.listen_allowed = CGRequestListenEventAccess();
    }

    if (needs_post_access) {
        status.post_allowed = CGPreflightPostEventAccess();
        if (!status.post_allowed) {
            status.post_allowed = CGRequestPostEventAccess();
        }
    } else {
        status.post_allowed = true;
    }

    return status;
}
