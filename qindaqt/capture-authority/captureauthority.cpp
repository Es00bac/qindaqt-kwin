// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"

#include "../session-lock/sessionlockcontroller.h"
#include "wayland/clientconnection.h"
#include "wayland_server.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QRandomGenerator>
#include <QSocketNotifier>
#include <QTimer>

#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace KWin
{
namespace
{
using namespace QindaQt::CaptureAuthority;

bool pidfdLive(const FileDescriptor &descriptor)
{
    if (!descriptor) {
        return false;
    }
    pollfd event{descriptor.get(), POLLIN, 0};
    return poll(&event, 1, 0) == 0;
}

bool uniqueSender(const QString &sender)
{
    return sender.startsWith(QStringLiteral(":")) && sender.size() >= 4
        && sender.size() <= 255 && !sender.contains(QChar::Null);
}

QString readySender(const CaptureWire::Packet &packet)
{
    const QByteArray bytes = packet.payload.mid(2);
    const QString sender = QString::fromUtf8(bytes);
    return sender.toUtf8() == bytes && uniqueSender(sender) ? sender : QString();
}

quint16 uint16(const char *data)
{
    return quint16(quint8(data[0])) | (quint16(quint8(data[1])) << 8);
}
}

CaptureAuthority::CaptureAuthority(WaylandServer *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    connect(server, &WaylandServer::screenAboutToLock, this, &CaptureAuthority::revokeAll,
            Qt::DirectConnection);
    connect(server, &WaylandServer::lockStateChanged, this, [this] {
        if (!privacyAllows()) {
            revokeAll();
        }
    });
    watchPortalOwners();
    auto *selected = new QDBusServiceWatcher(QStringLiteral("org.qindaqt.KWin"),
                                             QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(selected, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        if (!selectedCompositor()) {
            retireBroker();
        }
    });
}

CaptureAuthority::~CaptureAuthority()
{
    m_stopping = true;
    revokeAll();
    retireBroker();
}

bool CaptureAuthority::selectedCompositor() const
{
    auto bus = QDBusConnection::sessionBus();
    auto *daemon = bus.interface();
    if (!daemon || !bus.isConnected()) {
        return false;
    }
    const auto owner = daemon->serviceOwner(QStringLiteral("org.qindaqt.KWin"));
    const auto uid = daemon->serviceUid(bus.baseService());
    const auto pid = daemon->servicePid(bus.baseService());
    return owner.isValid() && owner.value() == bus.baseService()
        && uid.isValid() && uid.value() == uint(geteuid())
        && pid.isValid() && pid.value() == uint(getpid());
}

bool CaptureAuthority::privacyAllows() const
{
    // AGENT-GUARD: unknown native privacy is denial, even when legacy
    // desktop metadata or a private process identity otherwise matches.
    const auto *lock = m_server->sessionLock();
    return lock && !lock->isLocked() && selectedCompositor();
}

void CaptureAuthority::start()
{
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    // Test-only remote-input driver omits capture publication, never grants it.
    if (m_testBroker == QStringLiteral("@native-input-no-capture-broker")) {
        return;
    }
#endif
    if (m_stopping || m_broker) {
        return;
    }
    launchBroker();
}

void CaptureAuthority::launchBroker()
{
    if (m_stopping || m_broker || !selectedCompositor() || m_restartCount >= 3) {
        return;
    }
    ++m_restartCount;
    m_generation = QRandomGenerator::system()->generate64();
    if (!m_generation) {
        m_generation = 1;
    }
    m_highestJob = 0;
    m_brokerSender.clear();
    m_broker = std::make_unique<CaptureChild>(CaptureChild::Role::Broker, m_generation, 0,
                                              m_server->display(), this);
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (!m_testBroker.isEmpty()) {
        m_broker->setTestProgram(m_testBroker);
    }
#endif
    m_broker->packetReceived = [this](CaptureWire::Packet &&packet) { handleBrokerPacket(std::move(packet)); };
    m_broker->lost = [this] {
        QTimer::singleShot(0, this, [this] { retireBroker(); });
    };
    if (!m_broker->start()) {
        QTimer::singleShot(0, this, [this] { retireBroker(); });
    }
}

void CaptureAuthority::retireBroker()
{
    revokeAll();
    if (m_broker) {
        auto *broker = m_broker.release();
        broker->lost = {};
        broker->packetReceived = {};
        broker->stop();
        broker->deleteLater();
    }
    m_brokerSender.clear();
    if (!m_stopping && selectedCompositor() && m_restartCount < 3) {
        QTimer::singleShot(2000, this, [this] { launchBroker(); });
    }
}

bool CaptureAuthority::actorFromSender(const QString &sender, Actor *out) const
{
    auto *daemon = QDBusConnection::sessionBus().interface();
    if (!daemon || !uniqueSender(sender)) {
        return false;
    }
    const QDBusReply<QString> owner = daemon->serviceOwner(sender);
    const QDBusReply<uint> uid = daemon->serviceUid(sender);
    const QDBusReply<uint> pid = daemon->servicePid(sender);
    if (!owner.isValid() || owner.value() != sender || !uid.isValid()
        || uid.value() != uint(geteuid()) || !pid.isValid() || !pid.value()) {
        return false;
    }
    FileDescriptor pidfd(int(syscall(SYS_pidfd_open, pid.value(), 0)));
    const QDBusReply<uint> again = daemon->servicePid(sender);
    if (!pidfd || !again.isValid() || again.value() != pid.value() || !pidfdLive(pidfd)) {
        return false;
    }
    out->sender = sender;
    out->uid = uid.value();
    out->pid = pid.value();
    out->pidfd = std::move(pidfd);
    return true;
}

bool CaptureAuthority::actorLive(const Actor &actor) const
{
    auto *daemon = QDBusConnection::sessionBus().interface();
    if (!daemon || !pidfdLive(actor.pidfd)) {
        return false;
    }
    const QDBusReply<QString> owner = daemon->serviceOwner(actor.sender);
    const QDBusReply<uint> uid = daemon->serviceUid(actor.sender);
    const QDBusReply<uint> pid = daemon->servicePid(actor.sender);
    return owner.isValid() && owner.value() == actor.sender
        && uid.isValid() && uid.value() == actor.uid
        && pid.isValid() && pid.value() == uint(actor.pid);
}

bool CaptureAuthority::jobLive(const Job &job) const
{
    if (job.terminal || !m_broker || !m_broker->live() || m_brokerSender.isEmpty()
        || !job.helper || !job.helper->live() || !privacyAllows()
        || (job.helperReady && !actorLive(job.helperActor))
        || !actorLive(job.frontend) || !actorLive(job.caller)) {
        return false;
    }
    auto *daemon = QDBusConnection::sessionBus().interface();
    const auto backendOwner = daemon->serviceOwner(QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture"));
    const auto frontendOwner = daemon->serviceOwner(QStringLiteral("org.freedesktop.portal.Desktop"));
    return backendOwner.isValid() && backendOwner.value() == m_brokerSender
        && frontendOwner.isValid() && frontendOwner.value() == job.frontend.sender;
}

void CaptureAuthority::watchActor(quint64 id, const Actor &actor)
{
    auto *watcher = new QDBusServiceWatcher(actor.sender, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, id] { revokeJob(id); });
    auto *pidfd = new QSocketNotifier(actor.pidfd.get(), QSocketNotifier::Read, watcher);
    connect(pidfd, &QSocketNotifier::activated, this, [this, id] { revokeJob(id); });
    connect(this, &CaptureAuthority::jobRevoked, watcher, [watcher, pidfd, id](quint64 revoked) {
        if (revoked == id) {
            pidfd->setEnabled(false);
            watcher->deleteLater();
        }
    });
}

void CaptureAuthority::handleBrokerPacket(CaptureWire::Packet &&packet)
{
    using CaptureWire::Public::Message;
    if (!m_broker || !m_broker->live() || !selectedCompositor()) {
        retireBroker();
        return;
    }
    if (m_brokerSender.isEmpty()) {
        if (packet.type != Message::Ready || packet.job != 0) {
            retireBroker();
            return;
        }
        const QString sender = readySender(packet);
        Actor actor;
        auto *daemon = QDBusConnection::sessionBus().interface();
        const auto owner = daemon ? daemon->serviceOwner(QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture"))
                                  : QDBusReply<QString>();
        if (!actorFromSender(sender, &actor) || actor.pid != m_broker->pid()
            || !owner.isValid() || owner.value() != sender) {
            retireBroker();
            return;
        }
        m_brokerSender = sender;
        return;
    }
    if (packet.type == Message::StartJob) {
        startJob(packet);
    } else if (packet.type == Message::RevokeJob) {
        revokeJob(packet.job);
    } else {
        retireBroker();
    }
}

void CaptureAuthority::startJob(const CaptureWire::Packet &packet)
{
    using CaptureWire::Public::Message;
    using CaptureWire::Public::Error;
    const quint64 id = packet.job;
    if (id <= m_highestJob || m_jobs.size() >= CaptureWire::Public::MaxJobs
        || !privacyAllows() || !m_broker || !m_broker->live()) {
        if (m_broker) {
            QByteArray payload(2, '\0');
            payload[0] = char(m_jobs.size() >= CaptureWire::Public::MaxJobs ? Error::Limit : Error::Denied);
            m_broker->sendJob(Message::Error, id, payload);
        }
        return;
    }
    m_highestJob = id;
    const auto &data = packet.payload;
    const auto scope = CaptureWire::Public::Scope(uint16(data.constData()));
    const int frontendBytes = uint16(data.constData() + 2);
    const int callerBytes = uint16(data.constData() + 4);
    const QString frontend = QString::fromUtf8(data.mid(8, frontendBytes));
    const QString caller = QString::fromUtf8(data.mid(8 + frontendBytes, callerBytes));
    auto *daemon = QDBusConnection::sessionBus().interface();
    const auto portalOwner = daemon ? daemon->serviceOwner(QStringLiteral("org.freedesktop.portal.Desktop"))
                                    : QDBusReply<QString>();
    auto job = std::make_unique<Job>();
    job->id = id;
    job->scope = scope;
    if (!portalOwner.isValid() || portalOwner.value() != frontend
        || !actorFromSender(frontend, &job->frontend) || !actorFromSender(caller, &job->caller)) {
        QByteArray payload(2, '\0');
        payload[0] = char(Error::Denied);
        m_broker->sendJob(Message::Error, id, payload);
        return;
    }
    job->helper = std::make_unique<CaptureChild>(CaptureChild::Role::Helper, m_generation, id,
                                                 m_server->display(), this);
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (!m_testHelper.isEmpty()) {
        job->helper->setTestProgram(m_testHelper);
    }
#endif
    job->helper->packetReceived = [this, id](CaptureWire::Packet &&received) {
        handleHelperPacket(id, std::move(received));
    };
    job->helper->lost = [this, id] { revokeJob(id); };
    if (!job->helper->start() || !job->helper->captureClient() || !job->helper->guiClient()) {
        QByteArray payload(2, '\0');
        payload[0] = char(Error::Denied);
        m_broker->sendJob(Message::Error, id, payload);
        return;
    }
    ClientConnection *capture = job->helper->captureClient();
    ClientConnection *gui = job->helper->guiClient();
    m_captureTags.insert(capture, id);
    m_guiTags.insert(gui, id);
    connect(capture, &QObject::destroyed, this, [this, capture] { m_captureTags.remove(capture); });
    connect(gui, &QObject::destroyed, this, [this, gui] { m_guiTags.remove(gui); });
    const FileDescriptor request = job->helper->takeRequestWriter();
    const FileDescriptor result = job->helper->takeResultReader();
    m_jobs.emplace(id, std::move(job));
    watchActor(id, m_jobs.at(id)->frontend);
    watchActor(id, m_jobs.at(id)->caller);
    if (!m_broker->sendJob(Message::JobStarted, id, {}, {request.get(), result.get()})) {
        revokeJob(id);
        return;
    }
    // The timer is only pre-consent. Long-lived ScreenCast and retained
    // Screenshot URI lifetimes are governed by explicit lineage/revocation.
    QTimer::singleShot(60000, this, [this, id] {
        const auto it = m_jobs.find(id);
        if (it != m_jobs.end() && !it->second->consent) {
            revokeJob(id);
        }
    });
}

void CaptureAuthority::handleHelperPacket(quint64 id, CaptureWire::Packet &&packet)
{
    using CaptureWire::Public::Message;
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end() || !jobLive(*it->second)) {
        revokeJob(id);
        return;
    }
    Job &job = *it->second;
    if (!job.helperReady) {
        const QString sender = readySender(packet);
        Actor actor;
        if (packet.type != Message::Ready || !actorFromSender(sender, &actor)
            || actor.pid != job.helper->pid()) {
            revokeJob(id);
            return;
        }
        job.helperActor = std::move(actor);
        job.helperSender = sender;
        m_helperSenders.insert(sender, job.helperActor.pid);
        job.helperReady = true;
        auto *watcher = new QDBusServiceWatcher(sender, QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForUnregistration, this);
        connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, id, sender, watcher] {
            m_helperSenders.remove(sender);
            revokeJob(id);
            watcher->deleteLater();
        });
        return;
    }
    if (packet.type == Message::RevokeJob) {
        revokeJob(id);
        return;
    }
    if (packet.type == Message::ParentReady && !job.parentReady && !job.captureReady) {
        job.parentReady = true;
    } else if (packet.type == Message::ConsentGranted && !job.consent && !job.captureReady) {
        job.consent = true;
    } else {
        revokeJob(id);
        return;
    }
    maybeGrant(job);
}

void CaptureAuthority::maybeGrant(Job &job)
{
    if (job.helperReady && job.parentReady && job.consent && !job.captureReady && jobLive(job)) {
        job.captureReady = job.helper->send(CaptureWire::Public::Message::CaptureReady);
        if (!job.captureReady) {
            revokeJob(job.id);
        }
    }
}

void CaptureAuthority::revokeJob(quint64 id)
{
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end() || it->second->terminal) {
        return;
    }
    auto job = std::move(it->second);
    m_jobs.erase(it);
    job->terminal = true;
    // AGENT-CONTRACT: emit before destroying the exact tagged wl_client so
    // ScreenCast closes its PipeWire stream and pending Screenshot writers
    // can establish their no-more-bytes barrier.
    Q_EMIT jobRevoked(id);
    if (m_broker && m_broker->live()) {
        m_broker->sendJob(CaptureWire::Public::Message::JobRevoked, id);
    }
    if (job->helper) {
        auto *helper = job->helper.release();
        helper->lost = {};
        helper->packetReceived = {};
        if (helper->live()) {
            helper->send(CaptureWire::Public::Message::JobRevoked);
        }
        helper->stop();
        helper->deleteLater();
    }
}

void CaptureAuthority::revokeAll()
{
    QList<quint64> ids;
    for (const auto &[id, job] : m_jobs) {
        ids.append(id);
    }
    for (const auto id : ids) {
        revokeJob(id);
    }
}

#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
void CaptureAuthority::setTestPrograms(const QString &broker, const QString &helper)
{
    Q_ASSERT(!m_broker && m_jobs.empty());
    m_testBroker = broker;
    m_testHelper = helper;
}

bool CaptureAuthority::testBrokerReady() const
{
    return m_broker && m_broker->live() && !m_brokerSender.isEmpty();
}
#endif
}
