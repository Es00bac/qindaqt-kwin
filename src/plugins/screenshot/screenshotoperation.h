// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "utils/filedescriptor.h"

#include <QDBusMessage>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <memory>
#include <optional>

namespace KWin
{
class CaptureAuthority;
struct ScreenShotWriteState;

struct ScreenShotInvocation
{
    QString sender;
    pid_t pid = 0;
    uid_t uid = 0;
    FileDescriptor pidfd;
    std::optional<quint64> protectedJob;
    bool legacyTestPermission = false;
};

// One delayed D-Bus reply and one pipe writer. Revocation closes the write
// gate synchronously; protected success follows full EOF, not writer start.
class ScreenShotSinkPipe2 : public QObject
{
public:
    ScreenShotSinkPipe2(int fd, QDBusMessage reply, ScreenShotInvocation &&invocation,
                        CaptureAuthority *authority, QObject *parent);
    ~ScreenShotSinkPipe2() override;

    bool live() const;
    void cancel();
    void flush(const QImage &image, const QVariantMap &attributes);
    void writerFinished(bool success);

private:
    void replyError(const QString &name, const QString &message);
    void replySuccess();
    void revoke();

    QDBusMessage m_reply;
    FileDescriptor m_pipe;
    ScreenShotInvocation m_invocation;
    QPointer<CaptureAuthority> m_authority;
    std::shared_ptr<ScreenShotWriteState> m_write;
    QVariantMap m_result;
    bool m_replySent = false;
    bool m_writerStarted = false;
};
}
