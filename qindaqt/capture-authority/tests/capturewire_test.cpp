// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturewire.h"

#include <QTest>

#include <sys/socket.h>
#include <unistd.h>

using namespace KWin;
using namespace QindaQt::CaptureAuthority;

class CaptureWireTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readyBindsActualKernelSender();
    void jobStartedCarriesExactlyTwoDescriptors();
    void rejectMissingCredentialsAndInvalidShape();
};

void CaptureWireTest::readyBindsActualKernelSender()
{
    int pair[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    QVERIFY(CaptureWire::enableCredentials(pair[1]));
    QByteArray ready;
    ready.append(char(5));
    ready.append(char(0));
    ready.append(":1.42", 5);
    QVERIFY(CaptureWire::send(pair[0], Wire::Message::Ready, 7, 0, ready));
    CaptureWire::Packet packet;
    QCOMPARE(CaptureWire::receive(pair[1], &packet), 1);
    QCOMPARE(packet.type, Wire::Message::Ready);
    QCOMPARE(packet.generation, quint64(7));
    QCOMPARE(packet.job, quint64(0));
    QCOMPARE(packet.uid, geteuid());
    QCOMPARE(packet.pid, getpid());
    close(pair[0]);
    close(pair[1]);
}

void CaptureWireTest::jobStartedCarriesExactlyTwoDescriptors()
{
    int pair[2];
    int pipes[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    QVERIFY(pipe(pipes) == 0);
    QVERIFY(CaptureWire::enableCredentials(pair[1]));
    QVERIFY(CaptureWire::send(pair[0], Wire::Message::JobStarted, 9, 12, {}, {pipes[0], pipes[1]}));
    CaptureWire::Packet packet;
    QCOMPARE(CaptureWire::receive(pair[1], &packet), 1);
    QCOMPARE(packet.fds.size(), size_t(2));
    QVERIFY(packet.fds[0] && packet.fds[1]);
    close(pipes[0]);
    close(pipes[1]);
    close(pair[0]);
    close(pair[1]);
}

void CaptureWireTest::rejectMissingCredentialsAndInvalidShape()
{
    int pair[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    // Without SO_PASSCRED the decoder must fail closed.
    QVERIFY(CaptureWire::send(pair[0], Wire::Message::Hello, 3, 0));
    CaptureWire::Packet packet;
    QCOMPARE(CaptureWire::receive(pair[1], &packet), -1);
    QVERIFY(CaptureWire::enableCredentials(pair[1]));
    // Raw malformed v1 packet: header advertises one payload byte but has none.
    QByteArray invalid(Wire::HeaderBytes, '\0');
    invalid[0] = 'Q'; invalid[1] = 'C'; invalid[2] = 'C'; invalid[3] = '1';
    invalid[4] = char(Wire::Version);
    invalid[6] = char(Wire::Message::Hello);
    invalid[8] = 3;
    invalid[24] = 1;
    QCOMPARE(::send(pair[0], invalid.constData(), invalid.size(), MSG_NOSIGNAL), ssize_t(invalid.size()));
    QCOMPARE(CaptureWire::receive(pair[1], &packet), -1);
    close(pair[0]);
    close(pair[1]);
}

QTEST_GUILESS_MAIN(CaptureWireTest)
#include "capturewire_test.moc"
