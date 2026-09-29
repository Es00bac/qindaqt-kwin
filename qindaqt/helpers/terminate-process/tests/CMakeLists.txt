# SPDX-License-Identifier: GPL-2.0-or-later
# Also reachable with QINDAQT_KWIN_BUILD_TESTS and BUILD_TESTING=OFF, so enable
# ctest for this directory: ctest --test-dir <build>/qindaqt/helpers/terminate-process/tests
enable_testing()
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)
add_executable(tst_terminateprocess tst_terminateprocess.cpp)
target_link_libraries(tst_terminateprocess PRIVATE qindaqt_terminate_process_core Qt::Test)
target_compile_definitions(tst_terminateprocess PRIVATE
    TERMINATE_PROCESS_BINARY="$<TARGET_FILE:qindaqt_terminate_process>")
add_dependencies(tst_terminateprocess qindaqt_terminate_process)
add_test(NAME qindaqt-terminate-process COMMAND tst_terminateprocess)
