// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "kwin_export.h"
#include <QObject>
#include <QSize>
#include <memory>
struct wl_resource;
namespace KWin
{
class ClientConnection;
class Display;
class OutputInterface;
class SurfaceInterface;
class SessionLockV1Interface;
class SessionLockSurfaceV1Interface;
class SessionLockManagerV1Private;
class SessionLockV1Private;
class SessionLockSurfaceV1Private;

// GUI-thread protocol objects; disconnecting a client never requests unlock.
class KWIN_EXPORT SessionLockManagerV1Interface : public QObject
{
    Q_OBJECT
public:
    explicit SessionLockManagerV1Interface(Display *display, QObject *parent = nullptr);
    ~SessionLockManagerV1Interface() override;
Q_SIGNALS:
    void lockRequested(SessionLockV1Interface *lock);

private:
    std::unique_ptr<SessionLockManagerV1Private> d;
};
class KWIN_EXPORT SessionLockV1Interface : public QObject
{
    Q_OBJECT
public:
    ~SessionLockV1Interface() override;
    ClientConnection *client() const;
    void sendLocked();
    void sendFinished();
Q_SIGNALS:
    void surfaceCreated(SessionLockSurfaceV1Interface *surface);
    void unlockRequested();
    void aboutToBeDestroyed();

private:
    SessionLockV1Interface(Display *display, wl_resource *resource);
    friend class SessionLockManagerV1Private;
    std::unique_ptr<SessionLockV1Private> d;
};
class KWIN_EXPORT SessionLockSurfaceV1Interface : public QObject
{
    Q_OBJECT
public:
    ~SessionLockSurfaceV1Interface() override;
    SurfaceInterface *surface() const;
    OutputInterface *output() const;
    quint32 configure(const QSize &size);
Q_SIGNALS:
    void validCommit();
    void aboutToBeDestroyed();

private:
    SessionLockSurfaceV1Interface(Display *display, SurfaceInterface *surface, OutputInterface *output, wl_resource *resource);
    friend class SessionLockV1Private;
    std::unique_ptr<SessionLockSurfaceV1Private> d;
};
}
