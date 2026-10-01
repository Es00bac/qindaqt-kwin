// SPDX-License-Identifier: GPL-2.0-or-later
#include <wayland-client.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
void global(void *, wl_registry *, uint32_t id, const char *name, uint32_t)
{
    std::printf("%u %s\n", id, name);
}

void removed(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener listener{global, removed};
}

int main(int argc, char **argv)
{
    // fd 3 is one disposable compositor-created test socketpair endpoint.
    const bool publicSocket = argc == 2 && std::strcmp(argv[1], "--public") == 0;
    wl_display *display = publicSocket ? wl_display_connect("qindaqt-8") : wl_display_connect_to_fd(3);
    if (!display) {
        return 2;
    }
    wl_registry *registry = wl_display_get_registry(display);
    if (!registry || wl_registry_add_listener(registry, &listener, nullptr) < 0
        || wl_display_roundtrip(display) < 0) {
        return 3;
    }
    if (argc == 3) {
        char *end = nullptr;
        const unsigned long id = std::strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || id == 0 || id > UINT32_MAX || !argv[2][0]) {
            return 4;
        }
        // Deliberately guess a global ID learned on a separate disposable
        // self-PID connection. The filter must reject the bind even though
        // the tagged connection never received this global announcement.
        const wl_interface interface{argv[2], 1, 0, nullptr, 0, nullptr};
        wl_proxy *bound = static_cast<wl_proxy *>(wl_registry_bind(registry, uint32_t(id), &interface, 1));
        if (!bound) {
            return 5;
        }
        const int result = wl_display_roundtrip(display);
        wl_proxy_destroy(bound);
        // Exit 10 means the compositor disconnected this forbidden bind.
        return result < 0 || wl_display_get_error(display) ? 10 : 0;
    }
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
