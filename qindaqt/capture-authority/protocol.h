// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

// Installed, Qt-free process boundary shared with the separately built
// QindaQt capture backend. See WIRE.md for framing, credentials and lifetime.
namespace QindaQt::CaptureAuthority::Wire
{
inline constexpr std::uint32_t Magic = 0x31434351; // "QCC1" on the wire, little endian.
inline constexpr std::uint16_t Version = 1;
inline constexpr std::uint16_t HeaderBytes = 32;
inline constexpr std::uint32_t MaxPayloadBytes = 16 * 1024;
inline constexpr std::uint16_t MaxFdsPerMessage = 2;
inline constexpr std::uint16_t MaxJobs = 16;

inline constexpr int ControlFd = 3;
inline constexpr int GuiWaylandFd = 4;
inline constexpr int CaptureWaylandFd = 5;
inline constexpr int RequestReadFd = 6;
inline constexpr int ResultWriteFd = 7;

// AGENT-CONTRACT: the portal's earlier outer 30s timer is authoritative.
// Fork invocation begins later; its 28s deadline leaves a 2s reply margin.
inline constexpr int ProtectedTotalMilliseconds = 30'000;
inline constexpr int ProtectedForkMilliseconds = 28'000;

enum class Message : std::uint16_t {
    Hello = 1,          // compositor -> launched broker/helper
    Ready = 2,          // protected broker/helper -> compositor
    StartJob = 3,       // broker -> compositor
    JobStarted = 4,     // compositor -> broker, request-write + result-read FDs
    ParentReady = 5,    // helper -> compositor, after foreign-parent import
    ConsentGranted = 6, // helper -> compositor, actual Allow/Share action
    CaptureReady = 7,   // compositor -> helper, exact interface grant active
    RevokeJob = 8,      // broker/helper -> compositor
    JobRevoked = 9,     // compositor -> broker/helper, terminal
    Error = 10,         // compositor -> broker/helper, terminal
};

enum class Scope : std::uint16_t {
    Screenshot = 1, // Includes PickColor; only ScreenShot2 is privileged.
    ScreenCast = 2, // Only zkde_screencast_unstable_v1 is privileged.
};

enum class Error : std::uint16_t {
    Invalid = 1,
    Stale = 2,
    Denied = 3,
    Privacy = 4,
    Limit = 5,
    Deadline = 6,
    Transport = 7,
};
}
