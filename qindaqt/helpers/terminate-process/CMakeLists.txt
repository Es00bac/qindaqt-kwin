# SPDX-License-Identifier: GPL-2.0-or-later
#
# AGENT-NOTE: native replacement for KWin's KAuth call into ksysguard's process
# helper, which ended a hung window's process when it belonged to root or
# another user (container-wm ADR-0289; Plasma-free plan, owner decision 4).
# killer-helper runs terminate-process through pkexec, and polkit asks for an
# administrator with the org.qindaqt.kwin.terminate-process action.

add_library(qindaqt_terminate_process_core STATIC processidentity.cpp processidentity.h)
target_include_directories(qindaqt_terminate_process_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(qindaqt_terminate_process_core PUBLIC cxx_std_20)
# No Qt in the root helper: it must not read the environment or load plugins.
set_target_properties(qindaqt_terminate_process_core PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)

add_executable(qindaqt_terminate_process main.cpp)
target_link_libraries(qindaqt_terminate_process PRIVATE qindaqt_terminate_process_core)
set_target_properties(qindaqt_terminate_process PROPERTIES
    OUTPUT_NAME terminate-process
    AUTOMOC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libexec/qindaqt-kwin")
install(TARGETS qindaqt_terminate_process DESTINATION "${KDE_INSTALL_LIBEXECDIR}/qindaqt-kwin")

# AGENT-CONTRACT: polkit matches the program pkexec runs against this path, and
# src/helpers/killer passes the same QINDAQT_KWIN_TERMINATE_PROCESS_BIN.
configure_file(org.qindaqt.kwin.terminate-process.policy.in
    "${CMAKE_CURRENT_BINARY_DIR}/org.qindaqt.kwin.terminate-process.policy" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.qindaqt.kwin.terminate-process.policy"
    DESTINATION "${KDE_INSTALL_DATADIR}/polkit-1/actions")

if (BUILD_TESTING OR QINDAQT_KWIN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
