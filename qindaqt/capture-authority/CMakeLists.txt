# SPDX-License-Identifier: GPL-2.0-or-later
target_sources(kwin PRIVATE capturelaunchpolicy.cpp capturewire.cpp capturechild.cpp captureauthority.cpp)
configure_file(launchpaths.h.in "${CMAKE_CURRENT_BINARY_DIR}/launchpaths.h" @ONLY)
target_include_directories(kwin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/launchpaths.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/qindaqt-kwin/capture-authority
    COMPONENT Devel)
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
