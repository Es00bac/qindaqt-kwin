/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// Behaviour of terminate-process, the root helper behind qindaqt-kwin's
// hung-window prompt. Tests signal only child processes of the test itself,
// which an unprivileged user may do, so they need no root and no polkit.
#include "processidentity.h"

#include <QProcess>
#include <QTest>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
using namespace QindaQt::TerminateProcess;

namespace
{
extern "C" void exitOnAbort(int)
{
    _exit(7);
}

// A child that sleeps until signalled. With handleAbort it exits with code 7 on
// SIGABRT (proves SIGABRT comes first); with ignoreAbort only SIGKILL ends it.
pid_t spawnSleeper(bool handleAbort, bool ignoreAbort)
{
    const pid_t pid = fork();
    if (pid == 0) {
        if (handleAbort) {
            signal(SIGABRT, exitOnAbort);
        } else if (ignoreAbort) {
            signal(SIGABRT, SIG_IGN);
        }
        for (;;) {
            pause();
        }
    }
    return pid;
}

int reap(pid_t pid)
{
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}
} // namespace

class TerminateProcessTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesOnlyPlainDecimals_data();
    void parsesOnlyPlainDecimals();
    void parsesProcStatWithHostileCommandNames();
    void rejectsTruncatedProcStat();
    void recognisesKernelThreads();
    void validatesRequests_data();
    void validatesRequests();
    void abortsFirstSoCrashHandlersRun();
    void killsWhenAbortIsIgnored();
    void leavesAReusedPidAlone();
    void reportsAVanishedProcess();
    void refusesItsOwnProcessTree();
    void helperBinaryReportsOutcomes();
};

void TerminateProcessTest::parsesOnlyPlainDecimals_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qulonglong>("value");
    QTest::newRow("digits") << QStringLiteral("12345") << true << 12345ULL;
    QTest::newRow("zero") << QStringLiteral("0") << true << 0ULL;
    QTest::newRow("max") << QStringLiteral("18446744073709551615") << true << 18446744073709551615ULL;
    QTest::newRow("overflow") << QStringLiteral("18446744073709551616") << false << 0ULL;
    QTest::newRow("empty") << QString() << false << 0ULL;
    QTest::newRow("minus") << QStringLiteral("-1") << false << 0ULL;
    QTest::newRow("plus") << QStringLiteral("+1") << false << 0ULL;
    QTest::newRow("space") << QStringLiteral(" 1") << false << 0ULL;
    QTest::newRow("suffix") << QStringLiteral("12a") << false << 0ULL;
}

void TerminateProcessTest::parsesOnlyPlainDecimals()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(qulonglong, value);
    const QByteArray bytes = text.toLatin1();
    const auto parsed = parseDecimal(std::string_view(bytes.constData(), size_t(bytes.size())));
    QCOMPARE(parsed.has_value(), valid);
    if (valid) {
        QCOMPARE(qulonglong(*parsed), value);
    }
}

void TerminateProcessTest::parsesProcStatWithHostileCommandNames()
{
    // A command name may contain spaces and ") S 1 2" look-alikes.
    const std::string line = "4242 (evil) S 1 2 (x) R 9 9 9 9 9 9 9 9) S 1 4242 4242 0 -1 4194560 100 0 0 0 5 3 0 0 20 0 1 0 987654 1000 1 "
                             "18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0\n";
    const auto stat = parseProcStat(line);
    QVERIFY(stat.has_value());
    QCOMPARE(stat->state, 'S');
    QCOMPARE(stat->flags, 4194560ULL);
    QCOMPARE(stat->startTime, 987654ULL);
}

void TerminateProcessTest::rejectsTruncatedProcStat()
{
    QVERIFY(!parseProcStat("").has_value());
    QVERIFY(!parseProcStat("1 (init").has_value());
    QVERIFY(!parseProcStat("1 (init) S 0 1 1 0 -1 4194560 100").has_value());
    QVERIFY(!parseProcStat("1 (init) SS 0 1 1 0 -1 4194560 100 0 0 0 5 3 0 0 20 0 1 0 77 1").has_value());
}

void TerminateProcessTest::recognisesKernelThreads()
{
    ProcessStat stat;
    stat.flags = 0x00208040; // PF_KTHREAD | PF_NOFREEZE | PF_FORKNOEXEC
    QVERIFY(isKernelThread(stat));
    stat.flags = 0x00400100;
    QVERIFY(!isKernelThread(stat));
    const auto self = readProcStat(getpid());
    QVERIFY(self.has_value());
    QVERIFY(!isKernelThread(*self));
    QVERIFY(self->startTime > 0);
}

void TerminateProcessTest::validatesRequests_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<bool>("valid");
    QTest::newRow("valid") << QStringList{QStringLiteral("4242"), QStringLiteral("987654")} << true;
    QTest::newRow("missing start time") << QStringList{QStringLiteral("4242")} << false;
    QTest::newRow("extra argument") << QStringList{QStringLiteral("4242"), QStringLiteral("1"), QStringLiteral("2")} << false;
    QTest::newRow("pid 0") << QStringList{QStringLiteral("0"), QStringLiteral("1")} << false;
    QTest::newRow("pid 1") << QStringList{QStringLiteral("1"), QStringLiteral("1")} << false;
    QTest::newRow("pid overflow") << QStringList{QStringLiteral("4294967296"), QStringLiteral("1")} << false;
    QTest::newRow("start time 0") << QStringList{QStringLiteral("4242"), QStringLiteral("0")} << false;
    QTest::newRow("negative pid") << QStringList{QStringLiteral("-5"), QStringLiteral("1")} << false;
}

void TerminateProcessTest::validatesRequests()
{
    QFETCH(QStringList, arguments);
    QFETCH(bool, valid);
    std::vector<QByteArray> storage{QByteArrayLiteral("terminate-process")};
    for (const QString &argument : std::as_const(arguments)) {
        storage.push_back(argument.toLatin1());
    }
    std::vector<const char *> argv;
    for (const QByteArray &item : storage) {
        argv.push_back(item.constData());
    }
    std::string error;
    const auto request = parseRequest(int(argv.size()), argv.data(), &error);
    QCOMPARE(request.has_value(), valid);
    QCOMPARE(error.empty(), valid);
}

void TerminateProcessTest::abortsFirstSoCrashHandlersRun()
{
    const pid_t child = spawnSleeper(true, false);
    QVERIFY(child > 0);
    QTest::qWait(50); // let the child install its handler
    const auto stat = readProcStat(child);
    QVERIFY(stat.has_value());
    QCOMPARE(terminate(Request{child, stat->startTime}, 4000ms, 2000ms), Outcome::Terminated);
    const int status = reap(child);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 7);
}

void TerminateProcessTest::killsWhenAbortIsIgnored()
{
    const pid_t child = spawnSleeper(false, true);
    QVERIFY(child > 0);
    QTest::qWait(50);
    const auto stat = readProcStat(child);
    QVERIFY(stat.has_value());
    QCOMPARE(terminate(Request{child, stat->startTime}, 200ms, 2000ms), Outcome::Terminated);
    const int status = reap(child);
    QVERIFY(WIFSIGNALED(status));
    QCOMPARE(WTERMSIG(status), SIGKILL);
}

void TerminateProcessTest::leavesAReusedPidAlone()
{
    const pid_t child = spawnSleeper(false, false);
    QVERIFY(child > 0);
    const auto stat = readProcStat(child);
    QVERIFY(stat.has_value());
    // A start time that differs means the PID now names another process.
    QCOMPARE(terminate(Request{child, stat->startTime + 1}, 200ms, 200ms), Outcome::ProcessChanged);
    QCOMPARE(kill(child, 0), 0);
    kill(child, SIGKILL);
    reap(child);
}

void TerminateProcessTest::reportsAVanishedProcess()
{
    const pid_t child = spawnSleeper(false, false);
    QVERIFY(child > 0);
    const auto stat = readProcStat(child);
    QVERIFY(stat.has_value());
    kill(child, SIGKILL);
    reap(child);
    QCOMPARE(terminate(Request{child, stat->startTime}, 200ms, 200ms), Outcome::NoSuchProcess);
}

void TerminateProcessTest::refusesItsOwnProcessTree()
{
    const auto self = readProcStat(getpid());
    QVERIFY(self.has_value());
    QCOMPARE(terminate(Request{getpid(), self->startTime}, 200ms, 200ms), Outcome::Refused);
    QCOMPARE(terminate(Request{1, 1}, 200ms, 200ms), Outcome::Refused);
}

void TerminateProcessTest::helperBinaryReportsOutcomes()
{
    QCOMPARE(QProcess::execute(QStringLiteral(TERMINATE_PROCESS_BINARY), {QStringLiteral("1"), QStringLiteral("1")}), int(Outcome::Usage));
    QCOMPARE(QProcess::execute(QStringLiteral(TERMINATE_PROCESS_BINARY), {}), int(Outcome::Usage));

    const pid_t child = spawnSleeper(true, false);
    QVERIFY(child > 0);
    QTest::qWait(50);
    const auto stat = readProcStat(child);
    QVERIFY(stat.has_value());
    const QStringList arguments{QString::number(child), QString::number(stat->startTime)};
    QCOMPARE(QProcess::execute(QStringLiteral(TERMINATE_PROCESS_BINARY), arguments), int(Outcome::Terminated));
    const int status = reap(child);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 7);
}

QTEST_GUILESS_MAIN(TerminateProcessTest)
#include "tst_terminateprocess.moc"
