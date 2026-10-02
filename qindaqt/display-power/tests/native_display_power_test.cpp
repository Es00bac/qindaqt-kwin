// SPDX-License-Identifier: GPL-2.0-or-later
// ADR-0338: real native D-Bus admission over deterministic nested outputs.
#include "kwin_wayland_test.h"
#include "backends/virtual/virtual_backend.h"
#include "backends/virtual/virtual_output.h"
#include "core/outputconfiguration.h"
#include "main.h"
#include "wayland_server.h"
#include "workspace.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <linux/input.h>

namespace KWin
{
namespace
{
constexpr auto path = "/org/qindaqt/KWin/DisplayPower";
constexpr auto interface = "org.qindaqt.KWin.DisplayPower1";
constexpr auto session = "org.qindaqt.Session1";
constexpr auto id = "0123456789abcdef0123456789abcdef";

// AGENT-GUARD: stock VirtualOutput does NOT support DPMS. Only this private,
// noninstalled subclass advertises it. Actual applyChanges/mode completion,
// Workspace routing and the production authority are never replaced.
class FixtureOutput final : public VirtualOutput
{
public:
    FixtureOutput(VirtualBackend *backend, int index)
        : VirtualOutput(backend, false, {}, OutputTransform::Normal, {}, {},
                        QStringLiteral("Native-Power-%1").arg(index), {})
    {
        init(QPoint(index * 800, 0), QSize(800, 600), 1, {});
    }
    void capable(bool value)
    {
        auto information = m_information;
        information.capabilities.setFlag(Capability::Dpms, value);
        setInformation(information);
    }
    void mode(DpmsMode value)
    {
        auto state = m_state;
        state.dpmsMode = value;
        setState(state);
    }
    void placeholder(bool value)
    {
        auto information = m_information;
        information.placeholder = value;
        setInformation(information);
    }
};
class FixtureBackend final : public VirtualBackend
{
public:
    bool initialize() override
    {
        for (int index = 0; index < 2; ++index) {
            auto *output = new FixtureOutput(this, index);
            m_outputs.append(output);
            Q_EMIT outputAdded(output);
        }
        Q_EMIT outputsQueried();
        return true;
    }
    QList<BackendOutput *> outputs() const override
    {
        QList<BackendOutput *> result;
        for (auto *output : m_outputs) result.append(output);
        return result;
    }
    ~FixtureBackend() override
    {
        for (auto *output : m_outputs) output->unref();
    }
    QList<FixtureOutput *> m_outputs;
};
qulonglong deadline(int milliseconds = 5000)
{
    QElapsedTimer clock;
    clock.start();
    return qulonglong(clock.msecsSinceReference() + clock.elapsed() + milliseconds);
}
}

class NativeDisplayPowerTest final : public QObject
{
    Q_OBJECT
    FixtureBackend *m_backend = nullptr;
    qulonglong m_epoch = 0;
    bool m_available = false;
    bool m_admitted = false;
    QByteArray m_inventory;
    QString m_nonce;
    QString m_destination;
    int m_ended = 0;
    QString m_reason;
    QString m_request;
    int m_episode = 0;
    QDBusConnection client(const QString &name) const
    {
        return QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), name);
    }
    QDBusConnection authority() const { return client(QStringLiteral("native-power-authority")); }
    QDBusConnection ordinary() const { return client(QStringLiteral("native-power-ordinary")); }
    QDBusMessage call(const QDBusConnection &bus, const QString &method, const QVariantList &args = {})
    {
        auto request = QDBusMessage::createMethodCall(m_destination, path, interface, method);
        request.setArguments(args);
        auto pending = bus.asyncCall(request, 2000);
        if (!QTest::qWaitFor([&] { return pending.isFinished(); }, 2500))
            return QDBusMessage::createError(QStringLiteral("org.freedesktop.DBus.Error.Timeout"), method);
        return pending.reply();
    }
    void expect(const QDBusConnection &bus, const QString &method, const QVariantList &args, bool accepted)
    {
        const auto reply = call(bus, method, args);
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QCOMPARE(reply.signature(), QStringLiteral("b"));
        QCOMPARE(reply.arguments().at(0).toBool(), accepted);
    }
    void inventory()
    {
        m_nonce.clear();
        const auto reply = call(authority(), QStringLiteral("RequestInventoryWithReceipt"), {QLatin1String(id)});
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QTRY_COMPARE_WITH_TIMEOUT(m_nonce, QLatin1String(id), 2500);
        QVERIFY(m_epoch != 0);
    }
private Q_SLOTS:
    void receipt(const QString &nonce, qulonglong epoch, bool available, bool admitted, const QByteArray &bytes)
    {
        m_nonce = nonce; m_epoch = epoch; m_available = available;
        m_admitted = admitted; m_inventory = bytes;
    }
    void ended(const QString &request, const QString &reason)
    {
        if (request == m_request) { ++m_ended; m_reason = reason; }
    }
    void initTestCase()
    {
        QVERIFY(waylandServer()->init(QStringLiteral("qindaqt-native-power-test")));
        auto backend = std::make_unique<FixtureBackend>();
        m_backend = backend.get();
        kwinApp()->setOutputBackend(std::move(backend));
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        config->group(QStringLiteral("Effect-Kscreen")).writeEntry("Duration", 0);
        kwinApp()->setConfig(config);
        kwinApp()->start();
        QTRY_COMPARE(workspace()->outputs().size(), 2);
        m_destination = QDBusConnection::sessionBus().baseService();
        QVERIFY(authority().isConnected());
        QVERIFY(ordinary().isConnected());
        QVERIFY(authority().baseService() != m_destination);
        QVERIFY(ordinary().baseService() != authority().baseService());
        QVERIFY(authority().connect(m_destination, path, interface, "InventoryReceipt", this,
                                   SLOT(receipt(QString,qulonglong,bool,bool,QByteArray))));
        QVERIFY(authority().connect(m_destination, path, interface, "LeaseEnded", this,
                                   SLOT(ended(QString,QString))));
    }
    void init()
    {
        QVERIFY(authority().registerService(session));
        workspace()->requestDpmsState(Workspace::DpmsState::On);
        for (auto *output : m_backend->m_outputs) {
            output->placeholder(false); output->capable(true); output->mode(BackendOutput::DpmsMode::On);
        }
        m_request = QString::number(++m_episode, 16).rightJustified(32, QLatin1Char('0'));
        m_ended = 0; m_reason.clear();
        inventory();
        QVERIFY(m_available); QVERIFY(!m_admitted);
    }
    void cleanup()
    {
        expect(authority(), "SetNativeAdmission", {m_epoch, false}, true);
        QVERIFY(authority().unregisterService(session));
        ordinary().unregisterService(QStringLiteral("org.kde.Solid.PowerManagement"));
        workspace()->requestDpmsState(Workspace::DpmsState::On);
        for (auto *output : m_backend->m_outputs) output->mode(BackendOutput::DpmsMode::On);
    }
    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("native-power-authority"));
        QDBusConnection::disconnectFromBus(QStringLiteral("native-power-ordinary"));
    }
    void ordinaryCallerAndCurrentInventory()
    {
        const auto array = QJsonDocument::fromJson(m_inventory).array();
        QCOMPARE(array.size(), 2);
        for (const auto &entry : array) {
            const auto record = entry.toObject();
            QVERIFY(!record.value("id").toString().isEmpty());
            QVERIFY(record.value("dpms").toBool()); QVERIFY(record.value("on").toBool());
            QCOMPARE(record.value("geometry").toArray().size(), 4);
        }
        expect(ordinary(), "SetNativeAdmission", {m_epoch, true}, false);
        expect(ordinary(), "AcquireBlank", {m_epoch, m_request, deadline()}, false);
        expect(authority(), "SetNativeAdmission", {m_epoch + 1, true}, false);
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
    }
    void incompleteInventory_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("stock-virtual-unsupported") << 0;
        QTest::newRow("mixed-supported-unsupported") << 1;
        QTest::newRow("unexplained-mixed-off") << 2;
        QTest::newRow("all-off-without-provenance") << 3;
        QTest::newRow("missing-nonplaceholder-inventory") << 4;
    }
    void incompleteInventory()
    {
        QFETCH(int, variant);
        auto outputs = m_backend->m_outputs;
        if (variant == 0) for (auto *output : outputs) output->capable(false);
        if (variant == 1) outputs.last()->capable(false);
        if (variant == 2) outputs.last()->mode(BackendOutput::DpmsMode::Off);
        if (variant == 3) for (auto *output : outputs) output->mode(BackendOutput::DpmsMode::Off);
        if (variant == 4) for (auto *output : outputs) output->placeholder(true);
        inventory(); QVERIFY(!m_available);
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, false);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, false);
    }
    void legacyWriterAndTopologyLoss()
    {
        QVERIFY(ordinary().registerService(QStringLiteral("org.kde.Solid.PowerManagement")));
        inventory(); QVERIFY(!m_available);
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, false);
        QVERIFY(ordinary().unregisterService(QStringLiteral("org.kde.Solid.PowerManagement")));
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        m_backend->m_outputs.last()->capable(false);
        QTRY_COMPARE(m_reason, QStringLiteral("outputs-changed"));
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::On);
        inventory(); QVERIFY(!m_available); QVERIFY(!m_admitted);
    }
    void ownedBlankReleaseAndExternalOff()
    {
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        for (auto *output : m_backend->m_outputs) QCOMPARE(output->dpmsMode(), BackendOutput::DpmsMode::Off);
        expect(authority(), "ReleaseBlank", {m_epoch, m_request}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::On);
        expect(authority(), "AcquireBlank", {m_epoch, QStringLiteral("1123456789abcdef0123456789abcdef"), deadline()}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        workspace()->requestDpmsState(Workspace::DpmsState::Off); // same-mode external writer
        expect(authority(), "ReleaseBlank", {m_epoch, QStringLiteral("1123456789abcdef0123456789abcdef")}, true);
        QCOMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
    }
    void preexistingOffIsPreserved()
    {
        workspace()->requestDpmsState(Workspace::DpmsState::Off);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, true);
        expect(authority(), "ReleaseBlank", {m_epoch, m_request}, true);
        QCOMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
    }
    void wakeConsumesOwnedEpisode()
    {
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        Test::keyboardKeyPressed(KEY_A, 1);
        Test::keyboardKeyReleased(KEY_A, 2);
        QTRY_COMPARE(m_reason, QStringLiteral("woken"));
        QCOMPARE(m_ended, 1);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, false);
        QCOMPARE(workspace()->dpmsState(), Workspace::DpmsState::On);
    }
    void expiryAndAuthorityLoss()
    {
        expect(authority(), "SetNativeAdmission", {m_epoch, true}, true);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline(350)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(m_reason, QStringLiteral("expired"), 2500);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::On);
        expect(authority(), "AcquireBlank", {m_epoch, QStringLiteral("2123456789abcdef0123456789abcdef"), deadline()}, true);
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::Off);
        QVERIFY(authority().unregisterService(session));
        QTRY_COMPARE(workspace()->dpmsState(), Workspace::DpmsState::On);
        expect(authority(), "AcquireBlank", {m_epoch, m_request, deadline()}, false);
        QVERIFY(authority().registerService(session));
        inventory(); QVERIFY(!m_admitted);
    }
};
}
WAYLANDTEST_MAIN(KWin::NativeDisplayPowerTest)
#include "native_display_power_test.moc"
