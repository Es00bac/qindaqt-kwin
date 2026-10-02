// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "wayland/abstract_data_source.h"

#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QStringList>

#include <map>
#include <memory>

namespace KWin
{
// Seat selection published on behalf of one portal clipboard handle. Every
// Wayland paste request forwards the requester's write FD to the handle owner.
class EisClipboardSource final : public AbstractDataSource
{
    Q_OBJECT
public:
    EisClipboardSource(QStringList mimeTypes, std::function<void(const QString &, FileDescriptor)> transfer);
    ~EisClipboardSource() override;
    void requestData(const QString &mimeType, FileDescriptor fd) override;
    void cancel() override;
    QStringList mimeTypes() const override;

private:
    QStringList m_mimeTypes;
    std::function<void(const QString &, FileDescriptor)> m_transfer;
};

// AGENT-CONTRACT: RemoteDesktop clipboard adjunct for the QindaQt portal
// (docs/wiki/architecture/portal-remote-input.md). A handle belongs to the
// portal backend sender that connected it; consent and session lifetime stay
// in that backend. Selection data and paste FDs travel only in targeted
// signals to the owning sender, never broadcast. Compositor thread only.
class EisClipboard : public QObject
{
    Q_OBJECT
public:
    explicit EisClipboard(QObject *parent = nullptr);
    ~EisClipboard() override;

    int open(const QString &owner);
    bool close(const QString &owner, int handle);
    bool setSelection(const QString &owner, int handle, const QStringList &mimeTypes);
    QDBusUnixFileDescriptor readSelection(const QString &owner, int handle, const QString &mimeType);
    // Owner loss or native lock: drops handles and withdraws their selection.
    void removeOwner(const QString &owner);
    void clear();

private:
    struct Handle
    {
        QString owner;
        std::unique_ptr<EisClipboardSource> source;
    };
    bool owns(const QString &owner, int handle) const;
    void release(Handle &handle);
    void notifySelection();
    void transfer(int handle, const QString &mimeType, FileDescriptor fd);

    std::map<int, Handle> m_handles;
    int m_next = 0;
};
}
