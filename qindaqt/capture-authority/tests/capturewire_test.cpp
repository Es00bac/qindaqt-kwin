// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturewire.h"

#include <QTest>
#include <QtEndian>

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
    void rejectWrongDescriptorShape();
    void rejectMalformedFrames_data();
    void rejectMalformedFrames();
};

static QByteArray rawFrame(Wire::Message type, quint64 generation, quint64 job, const QByteArray &payload = {})
{
    QByteArray frame(Wire::HeaderBytes, '\0');
    qToLittleEndian<quint32>(Wire::Magic, reinterpret_cast<uchar *>(frame.data()));
    qToLittleEndian<quint16>(Wire::Version, reinterpret_cast<uchar *>(frame.data() + 4));
    qToLittleEndian<quint16>(quint16(type), reinterpret_cast<uchar *>(frame.data() + 6));
    qToLittleEndian<quint64>(generation, reinterpret_cast<uchar *>(frame.data() + 8));
    qToLittleEndian<quint64>(job, reinterpret_cast<uchar *>(frame.data() + 16));
    qToLittleEndian<quint32>(payload.size(), reinterpret_cast<uchar *>(frame.data() + 24));
    frame.append(payload);
    return frame;
}

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

void CaptureWireTest::rejectWrongDescriptorShape()
{
    int pair[2];
    int pipeEnds[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    QVERIFY(pipe(pipeEnds) == 0);
    QVERIFY(CaptureWire::enableCredentials(pair[1]));
    QVERIFY(!CaptureWire::send(pair[0], Wire::Message::JobStarted, 8, 1, {}, {pipeEnds[0]}));
    QVERIFY(!CaptureWire::send(pair[0], Wire::Message::Hello, 8, 0, {}, {pipeEnds[0]}));
    CaptureWire::Packet packet;
    QCOMPARE(CaptureWire::receive(pair[1], &packet), 0);
    close(pipeEnds[0]);
    close(pipeEnds[1]);
    close(pair[0]);
    close(pair[1]);
}

void CaptureWireTest::rejectMalformedFrames_data()
{
    QTest::addColumn<QByteArray>("frame");
    QByteArray frame = rawFrame(Wire::Message::Hello, 4, 0);
    frame[0] = 'X';
    QTest::newRow("bad-magic") << frame;
    frame = rawFrame(Wire::Message::Hello, 4, 0);
    frame[4] = 2;
    QTest::newRow("unknown-version") << frame;
    frame = rawFrame(Wire::Message(99), 4, 0);
    QTest::newRow("unknown-type") << frame;
    frame = rawFrame(Wire::Message::Hello, 0, 0);
    QTest::newRow("zero-generation") << frame;
    frame = rawFrame(Wire::Message::Hello, 4, 0);
    frame[30] = 1;
    QTest::newRow("reserved-header") << frame;
    frame = rawFrame(Wire::Message::Hello, 4, 0);
    frame[24] = 1;
    QTest::newRow("short-payload") << frame;
    frame = rawFrame(Wire::Message::Ready, 4, 0, QByteArray("\x05\0:1", 5));
    QTest::newRow("wrong-ready-length") << frame;
    frame = rawFrame(Wire::Message::StartJob, 4, 0, QByteArray(10, '\0'));
    QTest::newRow("zero-start-job") << frame;
    frame = rawFrame(Wire::Message::StartJob, 4, 1, QByteArray(10, '\0'));
    QTest::newRow("invalid-start-scope") << frame;
}

void CaptureWireTest::rejectMalformedFrames()
{
    QFETCH(QByteArray, frame);
    int pair[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    QVERIFY(CaptureWire::enableCredentials(pair[1]));
    QCOMPARE(::send(pair[0], frame.constData(), frame.size(), MSG_NOSIGNAL), ssize_t(frame.size()));
    CaptureWire::Packet packet;
    QCOMPARE(CaptureWire::receive(pair[1], &packet), -1);
    close(pair[0]);
    close(pair[1]);
}

QTEST_GUILESS_MAIN(CaptureWireTest)
#include "capturewire_test.moc"
