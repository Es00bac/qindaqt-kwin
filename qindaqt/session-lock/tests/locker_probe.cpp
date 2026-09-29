// SPDX-License-Identifier: GPL-2.0-or-later
// Non-installable native protocol probe. It grants no authentication and never
// requests lock/unlock; it proves the real inherited transport's global filter.
#include "qwayland-ext-session-lock-v1.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <wayland-client.h>
struct Probe
{
    QtWayland::ext_session_lock_manager_v1 manager;
};
static void announced(void *data, wl_registry *registry, uint32_t id, const char *name, uint32_t version)
{
    if (!strcmp(name, "ext_session_lock_manager_v1")) {
        static_cast<Probe *>(data)->manager.init(registry, id, version > 1 ? 1 : version);
    }
}
static void removed(void *, wl_registry *, uint32_t)
{
}
int main()
{
    const rlimit noCore{0, 0};
    if (setrlimit(RLIMIT_CORE, &noCore) || prctl(PR_SET_DUMPABLE, 0L)) {
        return 2;
    }
    wl_display *display = wl_display_connect(nullptr);
    if (!display) {
        return 3;
    }
    Probe probe;
    auto *registry = wl_display_get_registry(display);
    const wl_registry_listener listener{announced, removed};
    wl_registry_add_listener(registry, &listener, &probe);
    if (wl_display_roundtrip(display) < 0) {
        return 4;
    }
    printf("LOCK_GLOBAL=%d DUMPABLE=%d INJECTED=%d\n", probe.manager.isInitialized(), prctl(PR_GET_DUMPABLE), getenv("QINDAQT_FIXTURE_INJECTED") != nullptr);
    fflush(stdout);
    if (probe.manager.isInitialized()) {
        while (wl_display_dispatch(display) >= 0) { }
    }
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
