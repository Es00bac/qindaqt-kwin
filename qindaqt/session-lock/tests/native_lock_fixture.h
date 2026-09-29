// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/backendoutput.h"
#include "core/outputbackend.h"
#include "core/renderloop.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "qwayland-ext-session-lock-v1.h"
#include "scene/windowitem.h"
#include "sessionlockcontroller.h"
#include "wayland/display.h"
#include "wayland_server.h"
#include "workspace.h"
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/surface.h>

namespace KWin
{
class NativeLock : public QObject, public QtWayland::ext_session_lock_v1
{
public:
    explicit NativeLock(::ext_session_lock_v1 *object)
        : QtWayland::ext_session_lock_v1(object)
    {
    }
    ~NativeLock() override
    {
        if (isInitialized()) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(object()));
        }
    }
    bool locked = false;
    bool finished = false;
    void unlock()
    {
        unlock_and_destroy();
    }

protected:
    void ext_session_lock_v1_locked() override
    {
        locked = true;
    }
    void ext_session_lock_v1_finished() override
    {
        finished = true;
    }
};
class NativeSurface : public QtWayland::ext_session_lock_surface_v1
{
public:
    explicit NativeSurface(::ext_session_lock_surface_v1 *object)
        : QtWayland::ext_session_lock_surface_v1(object)
    {
    }
    ~NativeSurface() override
    {
        if (isInitialized()) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(object()));
        }
    }
    uint32_t serial = 0;
    QSize size;

protected:
    void ext_session_lock_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        this->serial = serial;
        size = QSize(width, height);
    }
};
struct NativeClient
{
    // AGENT-GUARD: authorization is per private socketpair, compiled only in
    // the non-installable test build; it never grants production clients access.
    std::unique_ptr<Test::Connection> connection;
    std::unique_ptr<KWayland::Client::Registry> registry;
    std::unique_ptr<QtWayland::ext_session_lock_manager_v1> manager;
    ~NativeClient()
    {
        if (manager) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(manager->object()));
        }
    }
    static std::unique_ptr<NativeClient> create(bool authorize, Test::AdditionalWaylandInterfaces interfaces = {})
    {
        auto client = std::make_unique<NativeClient>();
        QMetaObject::Connection grant;
        if (authorize) {
            grant = QObject::connect(waylandServer()->display(), &Display::clientConnected,
                                     waylandServer()->sessionLock(), [](ClientConnection *peer) {
                waylandServer()->sessionLock()->authorizeTestClient(peer);
            });
        }
        client->connection = Test::Connection::setup(interfaces);
        QObject::disconnect(grant);
        if (!client->connection) {
            return nullptr;
        }
        client->registry = std::make_unique<KWayland::Client::Registry>();
        client->registry->setEventQueue(client->connection->queue);
        QObject::connect(client->registry.get(), &KWayland::Client::Registry::interfaceAnnounced, client->registry.get(),
                         [c = client.get()](const QByteArray &interface, quint32 name, quint32 version) {
            if (interface == QByteArrayLiteral("ext_session_lock_manager_v1")) {
                c->manager = std::make_unique<QtWayland::ext_session_lock_manager_v1>();
                c->manager->init(*c->registry, name, version);
                c->connection->queue->addProxy(c->manager->object());
            }
        });
        QSignalSpy ready(client->registry.get(), &KWayland::Client::Registry::interfacesAnnounced);
        client->registry->create(client->connection->connection);
        client->registry->setup();
        if (!ready.wait()) {
            return nullptr;
        }
        return client;
    }
};
}
