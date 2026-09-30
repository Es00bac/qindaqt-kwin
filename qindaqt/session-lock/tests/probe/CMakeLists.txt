# SPDX-License-Identifier: GPL-2.0-or-later
# Separate binary directory prevents generated protocol sources from colliding
# with the main matrix target. These child probes are never installed.
add_executable(qindaqt-lock-probe ../locker_probe.cpp)
target_link_libraries(qindaqt-lock-probe Qt6::Core Wayland::Client)
qt6_generate_wayland_protocol_client_sources(qindaqt-lock-probe PRIVATE_CODE FILES ${WaylandProtocols_DATADIR}/staging/ext-session-lock/ext-session-lock-v1.xml)
add_library(qindaqt-lock-loader-witness MODULE ../loader_injection.cpp)
