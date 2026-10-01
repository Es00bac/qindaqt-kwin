// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "capturewire.h"

#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QSocketNotifier>

#include <array>
#include <functional>

namespace KWin
{
class ClientConnection;
class Display;

// One compositor-owned fixed-image child. It never admits an ordinary caller
// or accepts a process path, PID or Wayland FD from its protected peer.
class CaptureChild : public QObject
{
public:
    enum class Role { Broker, Helper };
    CaptureChild(Role role, quint64 generation, quint64 job, Display *display, QObject *parent = nullptr);
    ~CaptureChild() override;

    bool start();
    bool live() const;
    bool send(CaptureWire::Public::Message type, const QByteArray &payload = {}, const std::vector<int> &fds = {});
    bool sendJob(CaptureWire::Public::Message type, quint64 job, const QByteArray &payload = {}, const std::vector<int> &fds = {});
    FileDescriptor takeRequestWriter();
    FileDescriptor takeResultReader();
    ClientConnection *captureClient() const;
    ClientConnection *guiClient() const;
    pid_t pid() const { return m_pid; }
    quint64 generation() const { return m_generation; }
    quint64 job() const { return m_job; }
    Role role() const { return m_role; }
    void stop();
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    void setTestProgram(const QString &program);
#endif

    std::function<void(CaptureWire::Packet &&)> packetReceived;
    std::function<void()> lost;

private:
    bool makeControl();
    bool makeHelperDescriptors();
    bool duplicateChildDescriptor(int input, int target);
    void releaseChildDescriptors();
    void handleReadable();
    void fail();

    const Role m_role;
    const quint64 m_generation;
    const quint64 m_job;
    Display *m_display;
    QProcess m_process;
    FileDescriptor m_control;
    std::array<FileDescriptor, 5> m_childDescriptors;
    FileDescriptor m_requestWriter;
    FileDescriptor m_resultReader;
    FileDescriptor m_pidfd;
    QSocketNotifier *m_notifier = nullptr;
    QPointer<ClientConnection> m_guiClient;
    QPointer<ClientConnection> m_captureClient;
    pid_t m_pid = 0;
    bool m_failed = false;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    QString m_testProgram;
#endif
};
}
