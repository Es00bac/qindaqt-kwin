# SPDX-License-Identifier: GPL-2.0-or-later
# QindaQt additions that are not edits of upstream files (AGENTS.md).
add_subdirectory(helpers/terminate-process)
install(FILES capture-authority/protocol.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/qindaqt-kwin/capture-authority
    COMPONENT Devel)
