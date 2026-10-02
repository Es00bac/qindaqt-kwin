// SPDX-License-Identifier: GPL-2.0-or-later
#include "eisclipboard.h"

#include "utils/pipe.h"
#include "wayland/display.h"
#include "wayland/seat.h"
#include "wayland_server.h"

#include <QDBusConnection>
#include <QDBusMessage>

#include <fcntl.h>

namespace KWin
{
namespace
{
constexpr int maxHandles = 16;
constexpr int maxMimeTypes = 64;

void sendTargeted(const QString &owner, const QString &member, const QVariantList &arguments)
{
    auto signal = QDBusMessage::createTargetedSignal(owner, QStringLiteral("/org/kde/KWin/EIS/RemoteDesktop"),
                                                     QStringLiteral("org.kde.KWin.EIS.RemoteDesktop"), member);
    signal.setArguments(arguments);
    QDBusConnection::sessionBus().send(signal);
}
}

EisClipboardSource::EisClipboardSource(QStringList mimeTypes, std::function<void(const QString &, FileDescriptor)> transfer)
    : m_mimeTypes(std::move(mimeTypes))
    , m_transfer(std::move(transfer))
{
}

EisClipboardSource::~EisClipboardSource()
{
    // SeatInterface clears its selection on this signal.
    Q_EMIT aboutToBeDestroyed();
}

void EisClipboardSource::requestData(const QString &mimeType, FileDescriptor fd)
{
    if (m_mimeTypes.contains(mimeType)) {
        m_transfer(mimeType, std::move(fd));
    }
}

void EisClipboardSource::cancel()
{
}

QStringList EisClipboardSource::mimeTypes() const
{
    return m_mimeTypes;
}

EisClipboard::EisClipboard(QObject *parent)
    : QObject(parent)
{
    QObject::connect(waylandServer()->seat(), &SeatInterface::selectionChanged, this, &EisClipboard::notifySelection);
}

EisClipboard::~EisClipboard()
{
    clear();
}

bool EisClipboard::owns(const QString &owner, int handle) const
{
    const auto it = m_handles.find(handle);
    return it != m_handles.end() && it->second.owner == owner;
}

int EisClipboard::open(const QString &owner)
{
    if (m_handles.size() >= size_t(maxHandles)) {
        return 0;
    }
    const int handle = ++m_next;
    m_handles.emplace(handle, Handle{owner, nullptr});
    // Initial owner state follows the method reply on the same connection.
    QMetaObject::invokeMethod(this, &EisClipboard::notifySelection, Qt::QueuedConnection);
    return handle;
}

void EisClipboard::release(Handle &handle)
{
    if (!handle.source) {
        return;
    }
    auto *server = waylandServer();
    if (server && server->seat() && server->seat()->selection() == handle.source.get()) {
        server->seat()->setSelection(nullptr, server->display()->nextSerial());
    }
    handle.source.reset();
}

bool EisClipboard::close(const QString &owner, int handle)
{
    if (!owns(owner, handle)) {
        return false;
    }
    release(m_handles.at(handle));
    m_handles.erase(handle);
    return true;
}

bool EisClipboard::setSelection(const QString &owner, int handle, const QStringList &mimeTypes)
{
    if (!owns(owner, handle) || mimeTypes.size() > maxMimeTypes) {
        return false;
    }
    auto &entry = m_handles.at(handle);
    release(entry);
    if (mimeTypes.isEmpty()) {
        return true;
    }
    entry.source = std::make_unique<EisClipboardSource>(mimeTypes, [this, handle](const QString &mimeType, FileDescriptor fd) {
        transfer(handle, mimeType, std::move(fd));
    });
    waylandServer()->seat()->setSelection(entry.source.get(), waylandServer()->display()->nextSerial());
    return true;
}

QDBusUnixFileDescriptor EisClipboard::readSelection(const QString &owner, int handle, const QString &mimeType)
{
    auto *selection = waylandServer()->seat()->selection();
    if (!owns(owner, handle) || !selection || !selection->mimeTypes().contains(mimeType)) {
        return {};
    }
    auto pipe = Pipe::create(O_CLOEXEC);
    if (!pipe) {
        return {};
    }
    selection->requestData(mimeType, std::move(pipe->writeEndpoint));
    return QDBusUnixFileDescriptor(pipe->readEndpoint.get());
}

void EisClipboard::transfer(int handle, const QString &mimeType, FileDescriptor fd)
{
    const auto it = m_handles.find(handle);
    if (it == m_handles.end() || !fd.isValid()) {
        return;
    }
    // AGENT-GUARD: the paste FD reaches only the owning portal backend; a
    // broadcast would hand another client's pipe to every bus peer.
    sendTargeted(it->second.owner, QStringLiteral("selectionTransfer"),
                 {handle, mimeType, QVariant::fromValue(QDBusUnixFileDescriptor(fd.get()))});
}

void EisClipboard::notifySelection()
{
    auto *selection = waylandServer()->seat()->selection();
    const QStringList mimeTypes = selection ? selection->mimeTypes() : QStringList{};
    for (const auto &[handle, entry] : m_handles) {
        const bool owned = selection && selection == entry.source.get();
        sendTargeted(entry.owner, QStringLiteral("selectionChanged"), {handle, mimeTypes, owned});
    }
}

void EisClipboard::removeOwner(const QString &owner)
{
    for (auto it = m_handles.begin(); it != m_handles.end();) {
        if (it->second.owner == owner) {
            release(it->second);
            it = m_handles.erase(it);
        } else {
            ++it;
        }
    }
}

void EisClipboard::clear()
{
    for (auto &[id, handle] : m_handles) {
        release(handle);
    }
    m_handles.clear();
}
}
