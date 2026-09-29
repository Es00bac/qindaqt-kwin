# SPDX-License-Identifier: GPL-2.0-or-later
target_sources(kwin PRIVATE sessionlock_v1.cpp sessionlockcontroller.cpp sessionlockwindow.cpp)
target_compile_definitions(kwin PRIVATE QINDAQT_SESSION_LOCK_EXECUTABLE="${KDE_INSTALL_FULL_BINDIR}/qindaqt-lock")
if(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    target_compile_definitions(kwin PUBLIC QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
endif()
