/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// terminate-process <pid> <start-time>
//
// Runs as root through pkexec (polkit action org.qindaqt.kwin.terminate-process)
// and ends the one hung process that qindaqt-kwin's prompt identified. The exit
// code is a QindaQt::TerminateProcess::Outcome.
#include "processidentity.h"

#include <cstdio>

using namespace std::chrono_literals;
using namespace QindaQt::TerminateProcess;

int main(int argc, char **argv)
{
    std::string error;
    const auto request = parseRequest(argc, argv, &error);
    if (!request) {
        std::fprintf(stderr, "terminate-process: %s\nusage: terminate-process <pid> <start-time>\n", error.c_str());
        return int(Outcome::Usage);
    }
    const Outcome outcome = terminate(*request, 4000ms, 2000ms);
    if (outcome != Outcome::Terminated) {
        std::fprintf(stderr, "terminate-process: %s\n", describe(outcome));
    }
    return int(outcome);
}
