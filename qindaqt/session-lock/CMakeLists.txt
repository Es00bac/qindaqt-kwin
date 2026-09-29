# SPDX-License-Identifier: GPL-2.0-or-later
target_sources(kwin PRIVATE sessionlock_v1.cpp sessionlockcontroller.cpp sessionlockwindow.cpp lockerlaunchpolicy.cpp lockerlauncher.cpp nativelockdbusinterface.cpp)
target_compile_definitions(kwin PRIVATE QINDAQT_SESSION_LOCK_EXECUTABLE="${KDE_INSTALL_FULL_BINDIR}/qindaqt-lock"
    QINDAQT_SESSION_LOCK_DESKTOP_FILE="${KDE_INSTALL_FULL_DATADIR}/applications/org.qindaqt.Lock.desktop")
if(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    target_compile_definitions(kwin PUBLIC QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
endif()

install(FILES org.qindaqt.KWin.NativeLock1.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
