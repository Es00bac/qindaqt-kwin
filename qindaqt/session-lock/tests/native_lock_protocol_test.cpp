// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include <cerrno>
namespace KWin
{
void NativeSessionLockTest::protocolErrors_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("destroy-locked") << QString("destroy");
    QTest::newRow("unlock-before-presented") << QString("early-unlock");
    QTest::newRow("duplicate-output") << QString("duplicate");
    QTest::newRow("existing-role") << QString("role");
    QTest::newRow("already-committed") << QString("constructed");
    QTest::newRow("commit-before-ack") << QString("no-ack");
    QTest::newRow("null-buffer") << QString("null");
    QTest::newRow("wrong-dimensions") << QString("dimensions");
    QTest::newRow("unknown-serial") << QString("serial");
    QTest::newRow("consumed-serial") << QString("consumed");
}
void NativeSessionLockTest::protocolErrors()
{
    QFETCH(QString, action);
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    const auto outputs = kwinApp()->outputBackend()->outputs();
    struct Held
    {
        QList<BackendOutput *> outputs;
        ~Held()
        {
            for (auto output : outputs) {
                output->renderLoop()->uninhibit();
            }
        }
    } held;
    if (action == "early-unlock") {
        held.outputs = outputs;
        for (auto output : outputs) {
            output->renderLoop()->inhibit();
        }
    }
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(waylandServer()->isScreenLocked());
    if (action != "early-unlock") {
        QTRY_VERIFY(lock.locked);
    }
    QSignalSpy error(client->connection->connection, &KWayland::Client::ConnectionThread::errorOccurred);
    auto surface = Test::createSurface(client->connection->compositor);
    std::unique_ptr<Test::XdgToplevel> xdg;
    std::unique_ptr<NativeSurface> role;
    std::unique_ptr<KWayland::Client::Surface> secondSurface;
    std::unique_ptr<NativeSurface> duplicate;
    if (action == "destroy") {
        lock.destroy();
    } else if (action == "early-unlock") {
        lock.unlock();
    } else {
        if (action == "role") {
            xdg = Test::createXdgToplevelSurface(client->connection->xdgShell, surface.get());
        }
        if (action == "constructed") {
            surface->commit(KWayland::Client::Surface::CommitFlag::None);
        }
        role = std::make_unique<NativeSurface>(lock.get_lock_surface(*surface, *client->connection->outputs[0]));
        if (action != "role" && action != "constructed") {
            QTRY_VERIFY(role->serial);
            if (action == "duplicate") {
                secondSurface = Test::createSurface(client->connection->compositor);
                duplicate = std::make_unique<NativeSurface>(lock.get_lock_surface(*secondSurface, *client->connection->outputs[0]));
            } else if (action == "no-ack") {
                surface->commit(KWayland::Client::Surface::CommitFlag::None);
            } else if (action == "serial") {
                role->ack_configure(role->serial + 1);
            } else if (action == "consumed") {
                role->ack_configure(role->serial);
                role->ack_configure(role->serial);
            } else {
                role->ack_configure(role->serial);
                if (action == "null") {
                    surface->commit(KWayland::Client::Surface::CommitFlag::None);
                }
                if (action == "dimensions") {
                    Test::render(client->connection->shm, surface.get(), role->size - QSize(1, 0), Qt::black);
                }
            }
        }
    }
    QTRY_VERIFY(client->connection->connection->hasError());
    QTRY_COMPARE(error.count(), 1);
    QCOMPARE(client->connection->connection->errorCode(), EPROTO);
    // KWayland releases wl_display on protocol error; its fatal error and
    // server-side disconnection are the stable client/server evidence.
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QVERIFY(waylandServer()->isScreenLocked());
}
}
