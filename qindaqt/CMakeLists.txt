# SPDX-License-Identifier: GPL-2.0-or-later
# QindaQt additions that are not edits of upstream files (AGENTS.md).
add_subdirectory(helpers/terminate-process)
install(FILES capture-authority/protocol.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/qindaqt-kwin/capture-authority
    COMPONENT Devel)

if(BUILD_TESTING OR QINDAQT_KWIN_BUILD_TESTS)
    add_subdirectory(service-permissions/tests)
endif()

if(QINDAQT_KWIN_BUILD_TESTS AND NOT BUILD_TESTING)
    add_subdirectory(shortcuts/tests)
endif()
