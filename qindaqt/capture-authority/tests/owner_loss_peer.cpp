// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturewire.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusUnixFileDescriptor>
#include <QSocketNotifier>
#include <QtEndian>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <unordered_map>

using namespace KWin;
namespace Wire = CaptureWire::Public;

// Disposable handshake actor only: no pixels, user consent or frontend policy
// are simulated as qualification evidence. Admission still uses real fd3,
// process credentials, unique bus owners and the compositor's launched pidfds.
class OwnerLossPeer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.qindaqt.Test.CaptureOwnerLoss")
public:
    explicit OwnerLossPeer(bool helper)
        : m_helper(helper)
        , m_control(Wire::ControlFd, QSocketNotifier::Read, this)
    {
        connect(&m_control, &QSocketNotifier::activated, this, &OwnerLossPeer::readControl);
    }

public Q_SLOTS:
    qulonglong StartJob(const QString &frontend, const QString &caller, int scope)
    {
        if (m_helper || !m_generation) return 0;
        const QByteArray a = frontend.toUtf8(), b = caller.toUtf8();
        QByteArray payload(8, '\0');
        qToLittleEndian<quint16>(scope, payload.data());
        qToLittleEndian<quint16>(a.size(), payload.data() + 2);
        qToLittleEndian<quint16>(b.size(), payload.data() + 4);
        payload += a + b;
        const quint64 id = ++m_highest;
        return CaptureWire::send(Wire::ControlFd, Wire::Message::StartJob, m_generation, id, payload) ? id : 0;
    }
    QString Helper(qulonglong id) const
    {
        const auto it = m_results.find(id);
        return it == m_results.end() ? QString() : it->second.sender;
    }
    bool Revoke(qulonglong id)
    {
        return !m_helper && CaptureWire::send(Wire::ControlFd, Wire::Message::RevokeJob, m_generation, id);
    }
    bool Release()
    {
        return !m_helper && QDBusConnection::sessionBus().unregisterService(
            QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture"));
    }
    bool Capture(QDBusUnixFileDescriptor pipe)
    {
        if (!m_helper || !m_granted || !pipe.isValid()) return false;
        auto call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.ScreenShot2"),
            QStringLiteral("/org/kde/KWin/ScreenShot2"), QStringLiteral("org.kde.KWin.ScreenShot2"), QStringLiteral("CaptureArea"));
        call.setArguments({0, 0, uint(1024), uint(768), QVariantMap{}, QVariant::fromValue(pipe)});
        // asyncCall serializes/owns its UnixFD; this slot retains no writer.
        QDBusConnection::sessionBus().asyncCall(call, 5000);
        return true;
    }

private:
    struct Result
    {
        FileDescriptor pipe;
        QByteArray bytes;
        QString sender;
    };
    void readControl()
    {
        for (;;) {
            CaptureWire::Packet packet;
            const int status = CaptureWire::receive(Wire::ControlFd, &packet);
            if (!status) return;
            if (status < 0) QCoreApplication::exit(2);
            if (status < 0) return;
            if (packet.type == Wire::Message::Hello && !m_generation) {
                m_generation = packet.generation;
                m_job = packet.job;
                const QByteArray sender = QDBusConnection::sessionBus().baseService().toUtf8();
                QByteArray payload(2, '\0');
                qToLittleEndian<quint16>(sender.size(), payload.data());
                payload += sender;
                if (!CaptureWire::send(Wire::ControlFd, Wire::Message::Ready, m_generation, m_job, payload)) QCoreApplication::exit(3);
                if (m_helper) {
                    CaptureWire::send(Wire::ControlFd, Wire::Message::ParentReady, m_generation, m_job);
                    CaptureWire::send(Wire::ControlFd, Wire::Message::ConsentGranted, m_generation, m_job);
                }
            } else if (packet.type == Wire::Message::CaptureReady && m_helper) {
                m_granted = true;
                const QByteArray report = QDBusConnection::sessionBus().baseService().toUtf8() + '\n';
                if (write(Wire::ResultWriteFd, report.constData(), report.size()) != report.size()) QCoreApplication::exit(4);
            } else if (packet.type == Wire::Message::JobStarted && !m_helper) {
                auto &result = m_results[packet.job];
                result.pipe = std::move(packet.fds[1]);
                auto *input = new QSocketNotifier(result.pipe.get(), QSocketNotifier::Read, this);
                connect(input, &QSocketNotifier::activated, this, [this, input, id = packet.job] {
                    auto &result = m_results.at(id);
                    char bytes[256];
                    const ssize_t count = read(result.pipe.get(), bytes, sizeof(bytes));
                    if (count > 0) {
                        result.bytes.append(bytes, count);
                        if (result.bytes.endsWith('\n')) result.sender = QString::fromUtf8(result.bytes.trimmed());
                    } else {
                        input->setEnabled(false);
                    }
                });
            } else if (packet.type == Wire::Message::JobRevoked && m_helper) {
                QCoreApplication::quit();
            }
        }
    }
    bool m_helper;
    bool m_granted = false;
    QSocketNotifier m_control;
    quint64 m_generation = 0;
    quint64 m_job = 0;
    quint64 m_highest = 0;
    std::unordered_map<quint64, Result> m_results;
};

int main(int argc, char **argv)
{
    const rlimit noCore{0, 0};
    if (setrlimit(RLIMIT_CORE, &noCore) || prctl(PR_SET_DUMPABLE, 0L)
        || prctl(PR_GET_DUMPABLE) != 0 || !CaptureWire::enableCredentials(Wire::ControlFd)) return 1;
    const bool helper = fcntl(Wire::GuiWaylandFd, F_GETFD) >= 0;
    QCoreApplication app(argc, argv);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return 1;
    if (!helper) {
        const auto result = bus.interface()->registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture"),
            QDBusConnectionInterface::DontQueueService, QDBusConnectionInterface::AllowReplacement);
        if (!result.isValid() || result.value() != QDBusConnectionInterface::ServiceRegistered) return 1;
    }
    OwnerLossPeer peer(helper);
    if (!bus.registerObject(QStringLiteral("/org/qindaqt/Test/CaptureOwnerLoss"), &peer, QDBusConnection::ExportAllSlots)) return 1;
    return app.exec();
}
#include "owner_loss_peer.moc"
