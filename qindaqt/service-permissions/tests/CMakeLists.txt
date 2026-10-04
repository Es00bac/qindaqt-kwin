# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(QindaQtServicePermissions LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Test)
    find_package(KF6Config REQUIRED)
    find_package(KF6Service REQUIRED)
    include(GenerateExportHeader)
    get_filename_component(FORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(process_path_source executable_path_proc.cpp)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(process_path_source executable_path_sysctl.cpp)
    else()
        message(FATAL_ERROR "Unsupported process-path platform ${CMAKE_SYSTEM_NAME}")
    endif()
    # Compile the production PID resolver, rather than substituting a test stub.
    add_library(qindaqt_permission_process_path STATIC "${FORK_ROOT}/src/utils/${process_path_source}")
    generate_export_header(qindaqt_permission_process_path BASE_NAME KWIN EXPORT_FILE_NAME kwin_export.h)
    target_include_directories(qindaqt_permission_process_path PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(qindaqt_permission_process_path PUBLIC Qt6::Core)
    set(process_path_target qindaqt_permission_process_path)

    # Compile the real production launcher without linking or starting it.
    # Full compositor/private-socket/PAM qualification remains a separate gate.
    add_library(qindaqt_permission_locker_compile OBJECT
        "${FORK_ROOT}/qindaqt/session-lock/lockerlauncher.cpp")
    target_include_directories(qindaqt_permission_locker_compile PRIVATE "${FORK_ROOT}/src")
    target_link_libraries(qindaqt_permission_locker_compile PRIVATE
        ${process_path_target} KF6::ConfigCore KF6::Service)
    target_compile_definitions(qindaqt_permission_locker_compile PRIVATE
        QINDAQT_SESSION_LOCK_EXECUTABLE="/usr/bin/qindaqt-lock"
        QINDAQT_SESSION_LOCK_DESKTOP_FILE="/usr/share/applications/org.qindaqt.Lock.desktop")

    add_executable(testNativePermissions_capture_policy
        "${FORK_ROOT}/qindaqt/capture-authority/tests/capturelaunchpolicy_test.cpp"
        "${FORK_ROOT}/qindaqt/capture-authority/capturelaunchpolicy.cpp")
    target_include_directories(testNativePermissions_capture_policy PRIVATE
        "${FORK_ROOT}/src" "${FORK_ROOT}/qindaqt/capture-authority")
    target_link_libraries(testNativePermissions_capture_policy PRIVATE
        ${process_path_target} Qt6::Test KF6::ConfigCore KF6::Service)
    add_test(NAME qindaqt-permission-capture-policy COMMAND testNativePermissions_capture_policy)
else()
    set(FORK_ROOT "${CMAKE_SOURCE_DIR}")
    set(process_path_target kwin)
endif()

# Also emit this directory's CTest file for helper-only compositor builds.
enable_testing()

foreach(test IN ITEMS permission_list service_lookup)
    add_executable(testNativePermissions_${test} "${test}_test.cpp")
    target_include_directories(testNativePermissions_${test} PRIVATE "${FORK_ROOT}/src")
    target_link_libraries(testNativePermissions_${test} PRIVATE ${process_path_target} Qt6::Test KF6::ConfigCore KF6::Service)
endforeach()
target_compile_definitions(testNativePermissions_permission_list PRIVATE
    QINDAQT_PERMISSION_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME qindaqt-permission-list COMMAND testNativePermissions_permission_list)
add_test(NAME qindaqt-permission-service-lookup
    COMMAND dbus-run-session -- $<TARGET_FILE:testNativePermissions_service_lookup>)
