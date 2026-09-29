// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessionlock_v1.h"
#include "qwayland-server-ext-session-lock-v1.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include "wayland/output.h"
#include "wayland/surface.h"
#include <QHash>
#include <QPointer>
#include <QQueue>
namespace KWin
{
class SessionLockManagerV1Private : public QtWaylandServer::ext_session_lock_manager_v1
{
public:
    SessionLockManagerV1Private(Display *display, SessionLockManagerV1Interface *q)
        : ext_session_lock_manager_v1(*display, 1)
        , display(display)
        , q(q)
    {
    }
    Display *display;
    SessionLockManagerV1Interface *q;
    void ext_session_lock_manager_v1_destroy(Resource *r) override
    {
        wl_resource_destroy(r->handle);
    }
    void ext_session_lock_manager_v1_lock(Resource *r, uint32_t id) override
    {
        auto resource = wl_resource_create(r->client(), &ext_session_lock_v1_interface, 1, id);
        if (!resource) {
            wl_resource_post_no_memory(r->handle);
            return;
        }
        auto lock = new SessionLockV1Interface(display, resource);
        Q_EMIT q->lockRequested(lock);
    }
};
class SessionLockV1Private : public QtWaylandServer::ext_session_lock_v1
{
public:
    SessionLockV1Private(Display *display, wl_resource *r, SessionLockV1Interface *q)
        : ext_session_lock_v1(r)
        , display(display)
        , q(q)
        , client(ClientConnection::get(wl_resource_get_client(r)))
    {
    }
    Display *display;
    SessionLockV1Interface *q;
    ClientConnection *client;
    bool locked = false;
    bool finished = false;
    QHash<OutputInterface *, QPointer<SessionLockSurfaceV1Interface>> surfaces;
    void ext_session_lock_v1_destroy_resource(Resource *) override
    {
        Q_EMIT q->aboutToBeDestroyed();
        delete q;
    }
    void ext_session_lock_v1_destroy(Resource *r) override
    {
        if (locked) {
            wl_resource_post_error(r->handle, error_invalid_destroy, "locked session requires unlock_and_destroy");
            return;
        }
        wl_resource_destroy(r->handle);
    }
    void ext_session_lock_v1_unlock_and_destroy(Resource *r) override
    {
        if (!locked) {
            wl_resource_post_error(r->handle, error_invalid_unlock, "locked event not sent");
            return;
        }
        Q_EMIT q->unlockRequested();
        wl_resource_destroy(r->handle);
    }
    void ext_session_lock_v1_get_lock_surface(Resource *r, uint32_t id, wl_resource *sr, wl_resource *out) override
    {
        auto surface = SurfaceInterface::get(sr);
        auto output = OutputInterface::get(out);
        if (!surface || !output || output->isRemoved()) {
            wl_resource_post_error(r->handle, error_duplicate_output, "invalid or removed output");
            return;
        }
        if (surface->role()) {
            wl_resource_post_error(r->handle, error_role, "surface already has a role");
            return;
        }
        if (!surface->isPristine()) {
            wl_resource_post_error(r->handle, error_already_constructed, "surface has attached or committed content");
            return;
        }
        if (surfaces.value(output)) {
            wl_resource_post_error(r->handle, error_duplicate_output, "output already has a lock surface");
            return;
        }
        auto resource = wl_resource_create(r->client(), &ext_session_lock_surface_v1_interface, 1, id);
        if (!resource) {
            wl_resource_post_no_memory(r->handle);
            return;
        }
        static SurfaceRole role(QByteArrayLiteral("ext_session_lock_surface_v1"));
        surface->setRole(&role);
        auto lockSurface = new SessionLockSurfaceV1Interface(display, surface, output, resource);
        surfaces.insert(output, lockSurface);
        QObject::connect(output, &QObject::destroyed, q, [this, output]() {
            surfaces.remove(output);
        });
        Q_EMIT q->surfaceCreated(lockSurface);
    }
};
struct SessionLockSurfaceCommit : public SurfaceAttachedState<SessionLockSurfaceCommit>
{
    std::optional<QSize> acknowledgedSize;
};
class SessionLockSurfaceV1Private : public SurfaceExtension<SessionLockSurfaceV1Private, SessionLockSurfaceCommit>, public QtWaylandServer::ext_session_lock_surface_v1
{
public:
    SessionLockSurfaceV1Private(Display *display, SurfaceInterface *surface, OutputInterface *output, wl_resource *r, SessionLockSurfaceV1Interface *q)
        : SurfaceExtension(surface)
        , ext_session_lock_surface_v1(r)
        , display(display)
        , output(output)
        , q(q)
    {
    }
    Display *display;
    QPointer<OutputInterface> output;
    SessionLockSurfaceV1Interface *q;
    QQueue<std::pair<quint32, QSize>> configures;
    std::optional<QSize> acknowledgedSize;
    void apply(SessionLockSurfaceCommit *commit)
    {
        if (commit->acknowledgedSize) {
            acknowledgedSize = commit->acknowledgedSize;
            commit->acknowledgedSize.reset();
        }
        if (!acknowledgedSize) {
            wl_resource_post_error(resource()->handle, error_commit_before_first_ack, "configure must be acknowledged");
            return;
        }
        if (!surface->buffer()) {
            wl_resource_post_error(resource()->handle, error_null_buffer, "lock surface cannot have a null buffer");
            return;
        }
        if (surface->size() != QSizeF(*acknowledgedSize)) {
            wl_resource_post_error(resource()->handle, error_dimensions_mismatch, "buffer must match acknowledged dimensions");
            return;
        }
        Q_EMIT q->validCommit();
    }
    void ext_session_lock_surface_v1_ack_configure(Resource *r, uint32_t serial) override
    {
        auto it = std::find_if(configures.begin(), configures.end(), [serial](const auto &c) {
            return c.first == serial;
        });
        if (it == configures.end()) {
            wl_resource_post_error(r->handle, error_invalid_serial, "unknown or consumed configure serial");
            return;
        }
        pending->acknowledgedSize = it->second;
        configures.erase(configures.begin(), std::next(it));
    }
    void ext_session_lock_surface_v1_destroy(Resource *r) override
    {
        wl_resource_destroy(r->handle);
    }
    void ext_session_lock_surface_v1_destroy_resource(Resource *) override
    {
        Q_EMIT q->aboutToBeDestroyed();
        delete q;
    }
};
SessionLockManagerV1Interface::SessionLockManagerV1Interface(Display *display, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SessionLockManagerV1Private>(display, this))
{
}
SessionLockManagerV1Interface::~SessionLockManagerV1Interface() = default;
SessionLockV1Interface::SessionLockV1Interface(Display *display, wl_resource *resource)
    : d(std::make_unique<SessionLockV1Private>(display, resource, this))
{
}
SessionLockV1Interface::~SessionLockV1Interface() = default;
ClientConnection *SessionLockV1Interface::client() const
{
    return d->client;
}
void SessionLockV1Interface::sendLocked()
{
    if (!d->locked && !d->finished) {
        d->locked = true;
        d->send_locked();
        d->client->flush();
    }
}
void SessionLockV1Interface::sendFinished()
{
    if (!d->finished && !d->locked) {
        d->finished = true;
        d->send_finished();
        d->client->flush();
    }
}
SessionLockSurfaceV1Interface::SessionLockSurfaceV1Interface(Display *display, SurfaceInterface *surface, OutputInterface *output, wl_resource *resource)
    : d(std::make_unique<SessionLockSurfaceV1Private>(display, surface, output, resource, this))
{
}
SessionLockSurfaceV1Interface::~SessionLockSurfaceV1Interface() = default;
SurfaceInterface *SessionLockSurfaceV1Interface::surface() const
{
    return d->surface;
}
OutputInterface *SessionLockSurfaceV1Interface::output() const
{
    return d->output;
}
quint32 SessionLockSurfaceV1Interface::configure(const QSize &size)
{
    auto serial = d->display->nextSerial();
    d->configures.push_back({serial, size});
    d->send_configure(serial, size.width(), size.height());
    d->surface->client()->flush();
    return serial;
}
}
