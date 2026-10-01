# SPDX-License-Identifier: GPL-2.0-or-later
target_sources(kwin PRIVATE capturelaunchpolicy.cpp capturewire.cpp capturechild.cpp captureauthority.cpp)
target_compile_definitions(kwin PRIVATE
    QINDAQT_CAPTURE_BROKER_EXECUTABLE="${KDE_INSTALL_FULL_LIBEXECDIR}/qindaqt-portal-capture-backend"
    QINDAQT_CAPTURE_BROKER_DESKTOP_FILE="${KDE_INSTALL_FULL_DATADIR}/applications/org.qindaqt.PortalCaptureBackend.desktop"
    QINDAQT_CAPTURE_HELPER_EXECUTABLE="${KDE_INSTALL_FULL_LIBEXECDIR}/qindaqt-portal-capture"
    QINDAQT_CAPTURE_HELPER_DESKTOP_FILE="${KDE_INSTALL_FULL_DATADIR}/applications/org.qindaqt.PortalCapture.desktop")
if(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    target_compile_definitions(kwin PUBLIC QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
endif()

if(BUILD_TESTING)
    add_executable(testCaptureWire tests/capturewire_test.cpp)
    target_sources(testCaptureWire PRIVATE capturewire.cpp)
    target_link_libraries(testCaptureWire PRIVATE kwin Qt::Test)
    target_include_directories(testCaptureWire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME kwin-testCaptureWire COMMAND testCaptureWire)
    ecm_mark_as_test(testCaptureWire)
endif()
