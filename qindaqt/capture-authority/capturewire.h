// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "protocol.h"
#include "utils/filedescriptor.h"

#include <QByteArray>

#include <sys/types.h>
#include <vector>

namespace KWin::CaptureWire
{
namespace Public = QindaQt::CaptureAuthority::Wire;

struct Packet
{
    Public::Message type{};
    quint64 generation = 0;
    quint64 job = 0;
    QByteArray payload;
    std::vector<FileDescriptor> fds;
    uid_t uid = 0;
    pid_t pid = 0;
};

bool enableCredentials(int fd);
// Returns 1 for a packet, 0 for EAGAIN, -1 for EOF or malformed input.
int receive(int fd, Packet *out);
bool send(int fd, Public::Message type, quint64 generation, quint64 job,
          const QByteArray &payload = {}, const std::vector<int> &fds = {});
}
