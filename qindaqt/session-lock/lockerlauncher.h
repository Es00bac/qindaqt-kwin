// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "kwin_export.h"
#include <QObject>
#include <QPointer>
#include <QProcess>
namespace KWin
{
class ClientConnection;
class Display;
// Compositor-thread launcher owns the sole production locker connection and
// native child. Display is borrowed for its lifetime. start() preserves an
// existing child; any validation/launch failure leaves the controller fenced.
class KWIN_EXPORT LockerLauncher final : public QObject
{
    Q_OBJECT
public:
    explicit LockerLauncher(Display *display, QObject *parent = nullptr);
    ~LockerLauncher() override;
    bool start();
    bool ownsConnection(ClientConnection *client) const;
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    // Non-installable fixtures can launch only their own disposable test child;
    // the production binary has neither this method nor path override storage.
    void setTestProgram(const QString &program);
    QByteArray readTestOutput();
    void stopTestFixture();
    qint64 testProcessId() const
    {
        return m_process.processId();
    }
    bool testIsRunning() const
    {
        return m_process.state() != QProcess::NotRunning;
    }
#endif
    QString error() const
    {
        return m_error;
    }

private:
    void releaseConnection();
    Display *m_display;
    QProcess m_process;
    QPointer<ClientConnection> m_connection;
    int m_childFd = -1;
    QString m_error;
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    QString m_testProgram;
#endif
};
}
