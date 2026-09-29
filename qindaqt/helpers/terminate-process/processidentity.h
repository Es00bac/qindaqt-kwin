/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>

// Terminating exactly one process, identified by PID and start time.
//
// AGENT-NOTE: this is the core of terminate-process, the root helper that
// qindaqt-kwin's hung-window prompt runs through pkexec when the frozen window
// belongs to root or another user (container-wm ADR-0289). It replaces KWin's
// KAuth call into ksysguard's process helper. It must stay free of Qt and any
// other library that reads the environment or loads plugins, because it runs
// as root; pkexec gives it a clean environment and nothing else.
namespace QindaQt::TerminateProcess
{

// What /proc/<pid>/stat reports about a process.
struct ProcessStat
{
    char state = '?';
    unsigned long long flags = 0; // field 9, the kernel's PF_* flags
    unsigned long long startTime = 0; // field 22, clock ticks after boot
};

// A request of `terminate-process <pid> <start-time>`.
struct Request
{
    pid_t pid = 0;
    unsigned long long startTime = 0;
};

// Exit codes of terminate-process; the unprivileged prompt reports them.
enum class Outcome : int {
    Terminated = 0,
    Usage = 1,
    Refused = 2, // PID 1, a kernel thread, or the helper's own process tree
    NoSuchProcess = 3, // already gone
    ProcessChanged = 4, // the PID now belongs to a different process
    SignalFailed = 5,
};

// A strictly decimal number: digits only, no sign or whitespace, no overflow.
std::optional<unsigned long long> parseDecimal(std::string_view text);

// Parses the contents of /proc/<pid>/stat. The command name (field 2) may hold
// spaces and parentheses, so fields are counted after its last ')'.
std::optional<ProcessStat> parseProcStat(std::string_view contents);

// Reads /proc/<pid>/stat; nullopt if the process does not exist.
std::optional<ProcessStat> readProcStat(pid_t pid);

bool isKernelThread(const ProcessStat &stat);

// Validates argv: exactly a PID above 1 and a non-zero start time.
std::optional<Request> parseRequest(int argc, const char *const *argv, std::string *error);

// Sends SIGABRT, so crash handlers and coredumpd can record the hang, and
// SIGKILL if the process has not exited after abortGrace.
//
// AGENT-GUARD: the process is addressed through a pidfd opened before its start
// time is checked, so a PID that is reused while the prompt is open is never
// signalled; keep the open-then-verify order.
Outcome terminate(const Request &request, std::chrono::milliseconds abortGrace, std::chrono::milliseconds killGrace);

const char *describe(Outcome outcome);

} // namespace QindaQt::TerminateProcess
