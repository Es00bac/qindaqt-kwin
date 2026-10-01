// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturewire.h"

#include <QtEndian>

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace KWin::CaptureWire
{
namespace
{
template<typename T>
T readLe(const char *data)
{
    return qFromLittleEndian<T>(reinterpret_cast<const uchar *>(data));
}

template<typename T>
void writeLe(char *data, T value)
{
    qToLittleEndian<T>(value, reinterpret_cast<uchar *>(data));
}

bool validShape(const Packet &packet)
{
    using Public::Message;
    const auto payloadSize = packet.payload.size();
    const auto fdCount = packet.fds.size();
    if (!packet.generation || (packet.job == 0 && packet.type != Message::Hello
                                && packet.type != Message::Ready && packet.type != Message::Error)) {
        return false;
    }
    switch (packet.type) {
    case Message::Hello:
    case Message::ParentReady:
    case Message::ConsentGranted:
    case Message::CaptureReady:
    case Message::RevokeJob:
    case Message::JobRevoked:
        return payloadSize == 0 && fdCount == 0;
    case Message::Ready: {
        if (payloadSize < 3 || fdCount != 0) {
            return false;
        }
        const quint16 nameBytes = readLe<quint16>(packet.payload.constData());
        return nameBytes >= 1 && nameBytes <= 255 && payloadSize == nameBytes + 2
            && !packet.payload.mid(2).contains('\0');
    }
    case Message::StartJob: {
        if (packet.job == 0 || payloadSize < 10 || fdCount != 0) {
            return false;
        }
        const quint16 scope = readLe<quint16>(packet.payload.constData());
        const quint16 frontend = readLe<quint16>(packet.payload.constData() + 2);
        const quint16 caller = readLe<quint16>(packet.payload.constData() + 4);
        const quint16 reserved = readLe<quint16>(packet.payload.constData() + 6);
        return (scope == quint16(Public::Scope::Screenshot) || scope == quint16(Public::Scope::ScreenCast))
            && frontend >= 1 && frontend <= 255 && caller >= 1 && caller <= 255
            && reserved == 0 && payloadSize == 8 + frontend + caller
            && !packet.payload.mid(8).contains('\0');
    }
    case Message::JobStarted:
        return packet.job != 0 && payloadSize == 0 && fdCount == 2;
    case Message::Error:
        return payloadSize == 2 && fdCount == 0
            && readLe<quint16>(packet.payload.constData()) >= quint16(Public::Error::Invalid)
            && readLe<quint16>(packet.payload.constData()) <= quint16(Public::Error::Transport);
    }
    return false;
}
}

bool enableCredentials(int fd)
{
    const int on = 1;
    return setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) == 0;
}

int receive(int fd, Packet *out)
{
    if (!out) {
        return -1;
    }
    std::array<char, Public::HeaderBytes + Public::MaxPayloadBytes> bytes{};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(ucred)) + CMSG_SPACE(2 * sizeof(int))> ancillary{};
    iovec iov{bytes.data(), bytes.size()};
    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.data();
    message.msg_controllen = ancillary.size();
    const ssize_t received = recvmsg(fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    if (received <= 0) {
        return -1;
    }
    Packet packet;
    bool credentials = false;
    bool unexpected = false;
    for (cmsghdr *header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET) {
            unexpected = true;
            continue;
        }
        if (header->cmsg_type == SCM_CREDENTIALS && header->cmsg_len == CMSG_LEN(sizeof(ucred)) && !credentials) {
            ucred peer{};
            std::memcpy(&peer, CMSG_DATA(header), sizeof(peer));
            packet.uid = peer.uid;
            packet.pid = peer.pid;
            credentials = true;
        } else if (header->cmsg_type == SCM_RIGHTS && header->cmsg_len >= CMSG_LEN(0)
                   && (header->cmsg_len - CMSG_LEN(0)) % sizeof(int) == 0) {
            const size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const auto *receivedFds = reinterpret_cast<const int *>(CMSG_DATA(header));
            for (size_t i = 0; i < count; ++i) {
                packet.fds.emplace_back(receivedFds[i]);
            }
        } else {
            unexpected = true;
        }
    }
    if (unexpected || !credentials || packet.pid <= 0 || message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)
        || received < Public::HeaderBytes || packet.fds.size() > Public::MaxFdsPerMessage
        || readLe<quint32>(bytes.data()) != Public::Magic
        || readLe<quint16>(bytes.data() + 4) != Public::Version
        || readLe<quint16>(bytes.data() + 30) != 0) {
        return -1;
    }
    packet.type = Public::Message(readLe<quint16>(bytes.data() + 6));
    packet.generation = readLe<quint64>(bytes.data() + 8);
    packet.job = readLe<quint64>(bytes.data() + 16);
    const quint32 payloadSize = readLe<quint32>(bytes.data() + 24);
    const quint16 fdCount = readLe<quint16>(bytes.data() + 28);
    if (payloadSize > Public::MaxPayloadBytes || received != Public::HeaderBytes + payloadSize
        || fdCount != packet.fds.size()) {
        return -1;
    }
    packet.payload = QByteArray(bytes.data() + Public::HeaderBytes, payloadSize);
    if (!validShape(packet)) {
        return -1;
    }
    *out = std::move(packet);
    return 1;
}

bool send(int fd, Public::Message type, quint64 generation, quint64 job,
          const QByteArray &payload, const std::vector<int> &fds)
{
    Packet packet;
    packet.type = type;
    packet.generation = generation;
    packet.job = job;
    packet.payload = payload;
    for (const int value : fds) {
        packet.fds.emplace_back(value < 0 ? -1 : dup(value));
    }
    if (payload.size() > Public::MaxPayloadBytes || fds.size() > Public::MaxFdsPerMessage
        || !validShape(packet)) {
        return false;
    }
    for (const auto &copy : packet.fds) {
        if (!copy) {
            return false;
        }
    }
    std::array<char, Public::HeaderBytes> header{};
    writeLe<quint32>(header.data(), Public::Magic);
    writeLe<quint16>(header.data() + 4, Public::Version);
    writeLe<quint16>(header.data() + 6, quint16(type));
    writeLe<quint64>(header.data() + 8, generation);
    writeLe<quint64>(header.data() + 16, job);
    writeLe<quint32>(header.data() + 24, payload.size());
    writeLe<quint16>(header.data() + 28, fds.size());
    iovec iov[2] = {{header.data(), header.size()}, {const_cast<char *>(payload.constData()), size_t(payload.size())}};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(2 * sizeof(int))> ancillary{};
    msghdr message{};
    message.msg_iov = iov;
    message.msg_iovlen = payload.isEmpty() ? 1 : 2;
    if (!fds.empty()) {
        message.msg_control = ancillary.data();
        message.msg_controllen = CMSG_SPACE(fds.size() * sizeof(int));
        auto *control = CMSG_FIRSTHDR(&message);
        control->cmsg_level = SOL_SOCKET;
        control->cmsg_type = SCM_RIGHTS;
        control->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
        std::memcpy(CMSG_DATA(control), fds.data(), fds.size() * sizeof(int));
    }
    const ssize_t expected = header.size() + payload.size();
    return sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL) == expected;
}
}
