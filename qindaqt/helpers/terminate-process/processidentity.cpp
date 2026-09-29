/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "processidentity.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace QindaQt::TerminateProcess
{

namespace
{
// PF_KTHREAD from include/linux/sched.h.
constexpr unsigned long long kernelThreadFlag = 0x00200000;

// Raw system calls: glibc's <sys/pidfd.h> declarations are not usable from C++
// on every supported glibc, and the helper needs nothing else from it.
int openPidfd(pid_t pid)
{
    return int(::syscall(SYS_pidfd_open, pid, 0));
}

int sendSignal(int pidfd, int signal)
{
    return int(::syscall(SYS_pidfd_send_signal, pidfd, signal, nullptr, 0));
}

class FileDescriptor
{
public:
    explicit FileDescriptor(int fd)
        : m_fd(fd)
    {
    }
    ~FileDescriptor()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    int get() const
    {
        return m_fd;
    }

private:
    int m_fd;
};

bool waitForExit(int pidfd, std::chrono::milliseconds timeout)
{
    pollfd entry{.fd = pidfd, .events = POLLIN, .revents = 0};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        const int result = ::poll(&entry, 1, left.count() > 0 ? int(left.count()) : 0);
        if (result > 0) {
            return true;
        }
        if (result == 0 || errno != EINTR) {
            return false;
        }
    }
}
} // namespace

std::optional<unsigned long long> parseDecimal(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const unsigned digit = unsigned(c - '0');
        if (value > (std::numeric_limits<unsigned long long>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

std::optional<ProcessStat> parseProcStat(std::string_view contents)
{
    const auto commandEnd = contents.rfind(')');
    if (commandEnd == std::string_view::npos) {
        return std::nullopt;
    }
    // Field 3 (state) starts two characters after the command's last ')'.
    std::string_view rest = contents.substr(commandEnd + 1);
    ProcessStat stat;
    int field = 3;
    while (!rest.empty() && field <= 22) {
        while (!rest.empty() && rest.front() == ' ') {
            rest.remove_prefix(1);
        }
        const auto end = rest.find_first_of(" \n");
        const std::string_view token = rest.substr(0, end);
        if (token.empty()) {
            break;
        }
        if (field == 3) {
            if (token.size() != 1) {
                return std::nullopt;
            }
            stat.state = token.front();
        } else if (field == 9 || field == 22) {
            const auto value = parseDecimal(token);
            if (!value) {
                return std::nullopt;
            }
            (field == 9 ? stat.flags : stat.startTime) = *value;
        }
        if (field == 22) {
            return stat;
        }
        ++field;
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end);
    }
    return std::nullopt;
}

std::optional<ProcessStat> readProcStat(pid_t pid)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/stat", int(pid));
    const FileDescriptor fd(::open(path, O_RDONLY | O_CLOEXEC));
    if (fd.get() < 0) {
        return std::nullopt;
    }
    char buffer[4096];
    std::string contents;
    while (true) {
        const ssize_t count = ::read(fd.get(), buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        contents.append(buffer, size_t(count));
    }
    return parseProcStat(contents);
}

bool isKernelThread(const ProcessStat &stat)
{
    return stat.flags & kernelThreadFlag;
}

std::optional<Request> parseRequest(int argc, const char *const *argv, std::string *error)
{
    if (argc != 3) {
        *error = "expected exactly two arguments";
        return std::nullopt;
    }
    const auto pid = parseDecimal(argv[1]);
    if (!pid || *pid <= 1 || *pid > (unsigned long long)std::numeric_limits<pid_t>::max()) {
        *error = "the process id must be a decimal number above 1";
        return std::nullopt;
    }
    const auto startTime = parseDecimal(argv[2]);
    if (!startTime || *startTime == 0) {
        *error = "the start time must be a positive decimal number";
        return std::nullopt;
    }
    return Request{.pid = pid_t(*pid), .startTime = *startTime};
}

Outcome terminate(const Request &request, std::chrono::milliseconds abortGrace, std::chrono::milliseconds killGrace)
{
    if (request.pid <= 1 || request.pid == ::getpid() || request.pid == ::getppid()) {
        return Outcome::Refused;
    }
    const FileDescriptor pidfd(openPidfd(request.pid));
    if (pidfd.get() < 0) {
        return errno == ESRCH ? Outcome::NoSuchProcess : Outcome::SignalFailed;
    }
    // The pidfd now pins one process. If the PID was reused after the prompt
    // read the start time, the start times differ and nothing is signalled.
    const auto stat = readProcStat(request.pid);
    if (!stat) {
        return Outcome::NoSuchProcess;
    }
    if (stat->startTime != request.startTime) {
        return Outcome::ProcessChanged;
    }
    if (isKernelThread(*stat)) {
        return Outcome::Refused;
    }
    if (sendSignal(pidfd.get(), SIGABRT) != 0) {
        return errno == ESRCH ? Outcome::NoSuchProcess : Outcome::SignalFailed;
    }
    if (waitForExit(pidfd.get(), abortGrace)) {
        return Outcome::Terminated;
    }
    if (sendSignal(pidfd.get(), SIGKILL) != 0) {
        return errno == ESRCH ? Outcome::Terminated : Outcome::SignalFailed;
    }
    return waitForExit(pidfd.get(), killGrace) ? Outcome::Terminated : Outcome::SignalFailed;
}

const char *describe(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Terminated:
        return "terminated";
    case Outcome::Usage:
        return "usage error";
    case Outcome::Refused:
        return "refused to signal PID 1, a kernel thread or the helper's own process tree";
    case Outcome::NoSuchProcess:
        return "the process no longer exists";
    case Outcome::ProcessChanged:
        return "the process id now belongs to a different process";
    case Outcome::SignalFailed:
        return "the process could not be signalled or did not exit";
    }
    return "unknown";
}

} // namespace QindaQt::TerminateProcess
