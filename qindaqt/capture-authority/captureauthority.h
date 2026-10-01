// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "capturechild.h"
#include "kwin_export.h"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{
class ClientConnection;
class WaylandServer;

// AGENT-CONTRACT: WaylandServer owns this compositor-thread object for its
// lifetime. Screenshot and ScreenCast plugins may query its exported API only
// on that thread; missing lineage, current native privacy or transport fails
// closed. jobRevoked is synchronous before the tagged client is destroyed, so
// plugin writers and streams must stop publication in their direct handlers.
// Portal Request/Session/app policy and publication remain in the separate
// capture-only broker (WIRE.md); this class never accepts ordinary callers.
class KWIN_EXPORT CaptureAuthority : public QObject
{
    Q_OBJECT
public:
    explicit CaptureAuthority(WaylandServer *server, QObject *parent = nullptr);
    ~CaptureAuthority() override;
    void start();

    bool isTaggedCaptureClient(ClientConnection *client) const;
    bool allowTaggedInterface(ClientConnection *client, const QByteArray &interfaceName) const;
    std::optional<quint64> streamJob(ClientConnection *client) const;
    bool streamAllowed(quint64 job) const;
    bool privacyAllows() const;

    // Returns a single consumed protected operation, or no protected grant.
    // The caller must still enforce ordinary legacy permission separately.
    std::optional<quint64> beginScreenshot(const QString &sender, pid_t pid);
    bool screenshotOperationAllowed(quint64 job, const QString &sender, pid_t pid) const;
    bool isProtectedHelperSender(const QString &sender, pid_t pid) const;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    void setTestPrograms(const QString &broker, const QString &helper);
#endif

Q_SIGNALS:
    void jobRevoked(quint64 job);

private:
    struct Actor
    {
        QString sender;
        FileDescriptor pidfd;
        pid_t pid = 0;
        uid_t uid = 0;
    };
    struct Job
    {
        quint64 id = 0;
        CaptureWire::Public::Scope scope{};
        Actor frontend;
        Actor caller;
        Actor helperActor;
        std::unique_ptr<CaptureChild> helper;
        QString helperSender;
        bool helperReady = false;
        bool parentReady = false;
        bool consent = false;
        bool captureReady = false;
        bool screenshotConsumed = false;
        bool terminal = false;
    };

    bool selectedCompositor() const;
    bool actorFromSender(const QString &sender, Actor *out) const;
    bool actorLive(const Actor &actor) const;
    bool jobLive(const Job &job) const;
    void launchBroker();
    void retireBroker();
    void handleBrokerPacket(CaptureWire::Packet &&packet);
    void handleHelperPacket(quint64 job, CaptureWire::Packet &&packet);
    void startJob(const CaptureWire::Packet &packet);
    void revokeJob(quint64 id);
    void revokeAll();
    void maybeGrant(Job &job);
    void watchActor(quint64 job, const Actor &actor);

    WaylandServer *m_server;
    std::unique_ptr<CaptureChild> m_broker;
    std::unordered_map<quint64, std::unique_ptr<Job>> m_jobs;
    QHash<ClientConnection *, quint64> m_captureTags;
    QHash<QString, pid_t> m_helperSenders;
    QString m_brokerSender;
    quint64 m_generation = 0;
    quint64 m_highestJob = 0;
    int m_restartCount = 0;
    bool m_stopping = false;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    QString m_testBroker;
    QString m_testHelper;
#endif
};
}
