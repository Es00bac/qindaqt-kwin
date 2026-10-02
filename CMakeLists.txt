cmake_minimum_required(VERSION 3.16)

set(PROJECT_VERSION "6.6.6") # Handled by release scripts
project(KWin VERSION ${PROJECT_VERSION})

# QindaQt: qindaqt-kwin's own version is the upstream base plus a fork serial.
# AGENT-CONTRACT: the serial matches the Gentoo package gui-wm/qindaqt-kwin-${PROJECT_VERSION}_p<serial>
# (the ebuild checks it) and container-wm's find_package(QindaQtKWin <version> EXACT).
# Bump it for every fork release; plugins built against another serial never load.
set(QINDAQT_KWIN_SERIAL 1)
set(QINDAQT_KWIN_VERSION "${PROJECT_VERSION}.${QINDAQT_KWIN_SERIAL}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_DEP_VERSION "6.6.6")
set(QT_MIN_VERSION "6.10.0")
set(KF6_MIN_VERSION "6.22.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.82")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

# where to look first for cmake modules, before ${CMAKE_ROOT}/Modules/ is checked
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${ECM_MODULE_PATH})

include(CMakeDependentOption)
include(CMakePackageConfigHelpers)
include(FeatureSummary)
include(WriteBasicConfigVersionFile)
include(GenerateExportHeader)
include(CheckCXXSourceCompiles)
include(CheckCXXCompilerFlag)
include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(KDEGitCommitHooks)

include(ECMFindQmlModule)
include(ECMInstallIcons)
include(ECMOptionalAddSubdirectory)
include(ECMConfiguredInstall)
include(ECMQtDeclareLoggingCategory)
include(ECMSetupQtPluginMacroNames)
include(ECMSetupVersion)
include(ECMQmlModule)
include(ECMGenerateQmlTypes)
include(ECMDeprecationSettings)
include(ECMGenerateQDoc)

option(KWIN_BUILD_DECORATIONS "Enable building of KWin decorations." ON)
# QindaQt: KWin's KCMs and KRunner plugin install into Plasma's shared plugin
# namespaces and would collide with stock KWin; QindaQt Settings replaces them.
option(KWIN_BUILD_KCMS "Enable building of KWin configuration modules." OFF)
option(KWIN_BUILD_NOTIFICATIONS "Enable building of KWin with knotifications support" ON)
option(KWIN_BUILD_SCREENLOCKER "Enable native session-lock enforcement (required)" ON)
if(NOT KWIN_BUILD_SCREENLOCKER)
    message(FATAL_ERROR "qindaqt-kwin requires native session-lock enforcement")
endif()
option(KWIN_BUILD_TABBOX "Enable building of KWin Tabbox functionality" ON)
option(KWIN_BUILD_X11 "Enable building X11 common code and Xwayland support" ON)
option(KWIN_NATIVE_SHORTCUTS "Use the QindaQt native shortcut authority (candidate, explicitly qualified by packaging)" OFF)
option(KWIN_BUILD_GLOBALSHORTCUTS "Enable building of KWin with global shortcuts support" ON)
option(KWIN_BUILD_RUNNERS "Enable building of KWin with krunner support" OFF)
# AGENT-GUARD: qindaqt-kwin must install beside an unmodified kde-plasma/kwin
# (container-wm ADR-0289). Both switches install stock-named files.
if (KWIN_BUILD_KCMS OR KWIN_BUILD_RUNNERS)
    message(FATAL_ERROR "qindaqt-kwin does not build KWin's KCMs or KRunner plugin: their files collide with stock KWin")
endif()

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Concurrent
    Core
    Core5Compat
    DBus
    Quick
    UiTools
    WaylandClient
    Widgets
    Svg
)

if (Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6GuiPrivate ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE)
endif()

find_package(Qt6Test ${QT_MIN_VERSION} CONFIG QUIET)
set_package_properties(Qt6Test PROPERTIES
    PURPOSE "Required for tests"
    TYPE OPTIONAL
)
add_feature_info("Qt6Test" Qt6Test_FOUND "Required for building tests")
if (NOT Qt6Test_FOUND)
    set(BUILD_TESTING OFF CACHE BOOL "Build the testing tree.")
endif()

option(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION "Permit native-test lock clients; installation forbidden" OFF)
if(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Lock test authorization requires BUILD_TESTING")
    endif()
    install(CODE [[message(FATAL_ERROR "Lock authorization test builds cannot be installed")]])
endif()
option(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION "Permit non-installed capture authority fixtures; installation forbidden" OFF)
if(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Capture authority test authorization requires BUILD_TESTING")
    endif()
    install(CODE [[message(FATAL_ERROR "Capture authority test builds cannot be installed")]])
endif()
if (BUILD_TESTING)
    find_package(KPipeWire)

    if (Qt6WaylandClient_VERSION VERSION_GREATER_EQUAL "6.10.0")
        find_package(Qt6WaylandClientPrivate ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE)
    endif()
endif()

# required frameworks by Core
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    ColorScheme
    Config
    CoreAddons
    Crash
    DBusAddons
    GlobalAccel
    GuiAddons
    I18n
    IdleTime
    Package
    Service
    Svg
    WidgetsAddons
    WindowSystem
)
# required frameworks by config modules
if(KWIN_BUILD_KCMS)
    find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
        Declarative
        KCMUtils
        NewStuff
        Service
        XmlGui
    )
endif()

if(KWIN_BUILD_TABBOX AND KWIN_BUILD_KCMS)
    find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
        WidgetsAddons
    )
endif()

# AGENT-CONTRACT: the nightlight plugin consumes QindaQt Schedule1 receipts;
# KNightTime discovery would retain the retired Plasma scheduler (ADR-0313).

find_package(Threads)
set_package_properties(Threads PROPERTIES
    PURPOSE "Needed for VirtualTerminal support in KWin Wayland"
    TYPE REQUIRED
)

find_package(KWayland ${PROJECT_DEP_VERSION} CONFIG)
set_package_properties(KWayland PROPERTIES
    PURPOSE "Required to build wayland platform plugin and tests"
    TYPE REQUIRED
)

# optional frameworks
find_package(KF6DocTools ${KF6_MIN_VERSION} CONFIG)
set_package_properties(KF6DocTools PROPERTIES
    PURPOSE "Enable building documentation"
    TYPE OPTIONAL
)
add_feature_info("KF6DocTools" KF6DocTools_FOUND "Enable building documentation")

find_package(KF6Kirigami ${KF6_MIN_VERSION} CONFIG)
set_package_properties(KF6Kirigami PROPERTIES
    DESCRIPTION "A QtQuick based components set"
    PURPOSE "Required at runtime for several QML effects"
    TYPE RUNTIME
)

# QindaQt: kdecoration is vendored (kdecoration/, container-wm ADR-0289). KWin
# links its private library, whose ABI moves with every Plasma release, so a
# shared copy would tie the fork to stock Plasma's release cadence.
add_subdirectory(kdecoration)

# QindaQt has its own decoration plugin and no classic Breeze/Aurorae backend.
# KWIN_BUILD_DECORATIONS remains enabled because it also controls the compiled
# placement default; it does not enable third-party decoration backends.

find_package(EGL)
set_package_properties(EGL PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required to build KWin with EGL support"
)

find_package(epoxy 1.3)
set_package_properties(epoxy PROPERTIES
    DESCRIPTION "libepoxy"
    URL "https://github.com/anholt/libepoxy"
    TYPE REQUIRED
    PURPOSE "OpenGL dispatch library"
)

find_package(Wayland 1.24)
set_package_properties(Wayland PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building KWin with Wayland support"
)

find_package(WaylandProtocols 1.45)
set_package_properties(WaylandProtocols PROPERTIES
    TYPE REQUIRED
    PURPOSE "Collection of Wayland protocols that add functionality not available in the Wayland core protocol"
    URL "https://gitlab.freedesktop.org/wayland/wayland-protocols/"
)
if (WaylandProtocols_VERSION VERSION_GREATER_EQUAL 1.47)
    set(HAVE_WAYLAND_PROTOCOLS_147 1)
else()
    set(HAVE_WAYLAND_PROTOCOLS_147 0)
endif()

find_package(PlasmaWaylandProtocols 1.20.0 CONFIG)
set_package_properties(PlasmaWaylandProtocols PROPERTIES
    TYPE REQUIRED
    PURPOSE "Collection of Plasma-specific Wayland protocols"
    URL "https://invent.kde.org/libraries/plasma-wayland-protocols/"
)

find_package(XKB 0.7.0)
set_package_properties(XKB PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building KWin with Wayland support"
)
if (XKB_VERSION VERSION_GREATER_EQUAL 1.5.0)
    set(HAVE_XKBCOMMON_NO_SECURE_GETENV 1)
else()
    set(HAVE_XKBCOMMON_NO_SECURE_GETENV 0)
endif()

find_package(Canberra REQUIRED)

if (KWIN_BUILD_X11)
    pkg_check_modules(XKBX11 IMPORTED_TARGET xkbcommon-x11 REQUIRED)
    add_feature_info(XKBX11 XKBX11_FOUND "Required for handling keyboard events in X11 backend")

    # All the required XCB components
    find_package(XCB 1.10 REQUIRED COMPONENTS
        COMPOSITE
        CURSOR
        DRI3
        ICCCM
        KEYSYMS
        PRESENT
        RANDR
        RENDER
        RES
        SHAPE
        SHM
        SYNC
        XCB
        XFIXES
        XINERAMA
        XINPUT
    )
    set_package_properties(XCB PROPERTIES TYPE REQUIRED)

    find_package(X11_XCB)
    set_package_properties(X11_XCB PROPERTIES
        PURPOSE "Required for building X11 windowed backend of kwin_wayland"
        TYPE OPTIONAL
    )

    find_package(Xwayland 23.1.0)
    set_package_properties(Xwayland PROPERTIES
        URL "https://x.org"
        DESCRIPTION "Xwayland X server"
        TYPE RUNTIME
        PURPOSE "Needed for running kwin_wayland"
    )
    set(HAVE_XWAYLAND_ENABLE_EI_PORTAL ${Xwayland_HAVE_ENABLE_EI_PORTAL})

    # for kwin internal things
    set(HAVE_X11_XCB ${X11_XCB_FOUND})

    find_package(X11)
    set_package_properties(X11 PROPERTIES
        DESCRIPTION "X11 libraries"
        URL "https://www.x.org"
        TYPE REQUIRED
    )

    # Scripts to run on XWayland startup
    set(XWAYLAND_SESSION_SCRIPTS "/etc/xdg/Xwayland-session.d")
endif()

find_package(Libinput 1.28)
set_package_properties(Libinput PROPERTIES TYPE REQUIRED PURPOSE "Required for input handling on Wayland.")

find_package(Libeis-1.0 1.4)
set_package_properties(Libeis-1.0 PROPERTIES TYPE OPTIONAL PURPOSE "Required for emulated input handling.")

find_package(UDev)
set_package_properties(UDev PROPERTIES
    URL "https://www.freedesktop.org/wiki/Software/systemd/"
    DESCRIPTION "Linux device library."
    TYPE REQUIRED
    PURPOSE "Required for input handling on Wayland."
)

find_package(Libdrm 2.4.118)
set_package_properties(Libdrm PROPERTIES TYPE REQUIRED PURPOSE "Required for drm output on Wayland.")
if (Libdrm_VERSION VERSION_GREATER_EQUAL 2.4.127)
    set(HAVE_LIBDRM_FAUX 1)
else()
    set(HAVE_LIBDRM_FAUX 0)
endif()


find_package(gbm)
set_package_properties(gbm PROPERTIES TYPE REQUIRED PURPOSE "Required for egl output of drm backend.")
if (gbm_VERSION VERSION_GREATER_EQUAL 21.1)
    set(HAVE_GBM_BO_GET_FD_FOR_PLANE 1)
else()
    set(HAVE_GBM_BO_GET_FD_FOR_PLANE 0)
endif()
if (gbm_VERSION VERSION_GREATER_EQUAL 21.3)
    set(HAVE_GBM_BO_CREATE_WITH_MODIFIERS2 1)
else()
    set(HAVE_GBM_BO_CREATE_WITH_MODIFIERS2 0)
endif()

pkg_check_modules(Libxcvt IMPORTED_TARGET libxcvt>=0.1.1 REQUIRED)
add_feature_info(Libxcvt Libxcvt_FOUND "Required for generating modes in the drm backend")

add_feature_info("XInput" X11_Xi_FOUND "Required for poll-free mouse cursor updates")
set(HAVE_X11_XINPUT ${X11_Xinput_FOUND})

find_package(lcms2)
set_package_properties(lcms2 PROPERTIES
    DESCRIPTION "Small-footprint color management engine"
    URL "http://www.littlecms.com"
    TYPE REQUIRED
    PURPOSE "Required for the color management system"
)

find_package(hwdata)
set_package_properties(hwdata PROPERTIES
    TYPE RUNTIME
    PURPOSE "Runtime-only dependency needed for mapping monitor hardware vendor IDs to full names"
    URL "https://github.com/vcrhonek/hwdata"
)

find_package(QAccessibilityClient6 CONFIG)
set_package_properties(QAccessibilityClient6 PROPERTIES
    URL "https://commits.kde.org/libqaccessibilityclient"
    DESCRIPTION "KDE client-side accessibility library"
    TYPE OPTIONAL
    PURPOSE "Required to enable accessibility features"
)

pkg_check_modules(libsystemd IMPORTED_TARGET libsystemd)
add_feature_info(libsystemd libsystemd_FOUND "Required for setting up the service watchdog")

if(KWIN_BUILD_GLOBALSHORTCUTS AND NOT KWIN_NATIVE_SHORTCUTS)
    find_package(KGlobalAccelD REQUIRED)
endif()

pkg_check_modules(libdisplayinfo REQUIRED IMPORTED_TARGET libdisplay-info>=0.2.0)
add_feature_info(libdisplayinfo libdisplayinfo_FOUND "EDID and DisplayID library: https://gitlab.freedesktop.org/emersion/libdisplay-info")

pkg_check_modules(PipeWire IMPORTED_TARGET libpipewire-0.3>=1.0.9)
add_feature_info(PipeWire PipeWire_FOUND "Required for Wayland screencasting")

if (KWIN_BUILD_NOTIFICATIONS)
    find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Notifications)
endif()


pkg_check_modules(libevdev IMPORTED_TARGET libevdev)
add_feature_info(libevdev libevdev_FOUND "Required for game controller support")

ecm_find_qmlmodule(QtQuick 2.3)
ecm_find_qmlmodule(QtQuick.Controls 2.15)
ecm_find_qmlmodule(QtQuick.Layouts 1.3)
ecm_find_qmlmodule(QtQuick.Window 2.1)
ecm_find_qmlmodule(org.kde.kquickcontrolsaddons 2.0)

set(KWIN_BUILD_ACTIVITIES OFF)
cmake_dependent_option(KWIN_BUILD_EIS "Enable building KWin with libeis support" ON "Libeis-1.0_FOUND" OFF)
cmake_dependent_option(KWIN_BUILD_QACCESSIBILITYCLIENT "Enable building KWin with libqaccessibilitysupport" ON "QAccessibilityClient6_FOUND" OFF)
cmake_dependent_option(KWIN_BUILD_GAMECONTROLLER "Enable building of KWin Game Controller functionality" ON "libevdev_FOUND" OFF)

include_directories(BEFORE
    ${CMAKE_CURRENT_BINARY_DIR}/src/wayland
    ${CMAKE_CURRENT_BINARY_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

check_symbol_exists(SCHED_RESET_ON_FORK "sched.h" HAVE_SCHED_RESET_ON_FORK)
add_feature_info("SCHED_RESET_ON_FORK"
                 HAVE_SCHED_RESET_ON_FORK
                 "Required for running kwin_wayland with real-time scheduling")

# clang < 16 does not support ranges and compiling KWin will fail in the middle of the build.
# clang 14 is still the default clang version on KDE Neon and Clazy is build with it
check_cxx_source_compiles("
#include <ranges>
#include <vector>

int main() {
    std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto even_numbers = numbers | std::views::filter([](int n) { return n % 2 == 0; });
    auto squared_numbers = even_numbers | std::views::transform([](int n) { return n * n; });
    return 0;
}
" HAS_RANGES_SUPPORT)
if(NOT HAS_RANGES_SUPPORT)
    message(FATAL_ERROR "Compiler does not support C++20 ranges")
endif()

check_cxx_source_compiles("
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    const int size = 10;
    int fd = memfd_create(\"test\", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    ftruncate(fd, size);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    mmap(nullptr, size, PROT_WRITE, MAP_SHARED, fd, 0);
    return 0;
}" HAVE_MEMFD)

check_cxx_compiler_flag(-Wno-unused-parameter COMPILER_UNUSED_PARAMETER_SUPPORTED)
if (COMPILER_UNUSED_PARAMETER_SUPPORTED)
    add_compile_options(-Wno-unused-parameter)
endif()

add_definitions(
    -DQT_NO_KEYWORDS
    -DQT_USE_QSTRINGBUILDER
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_TO_ASCII
    -DQT_NO_FOREACH

    # Prevent EGL headers from including platform headers, in particular Xlib.h.
    -DMESA_EGL_NO_X11_HEADERS
    -DEGL_NO_X11
    -DEGL_NO_PLATFORM_SPECIFIC_TYPES
)

ecm_set_disabled_deprecation_versions(QT 5.15
    KF 6.9.0
)

ecm_setup_version(${PROJECT_VERSION}
                  VARIABLE_PREFIX KWIN
                  SOVERSION 6
)
# QindaQt: consumers pin the exact fork release (see QINDAQT_KWIN_SERIAL).
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinConfigVersion.cmake"
    VERSION ${QINDAQT_KWIN_VERSION}
    COMPATIBILITY ExactVersion)

ecm_setup_qtplugin_macro_names(
    JSON_ARG2
        "KWIN_EFFECT_FACTORY"
    JSON_ARG3
        "KWIN_EFFECT_FACTORY_ENABLED"
        "KWIN_EFFECT_FACTORY_SUPPORTED"
    JSON_ARG4
        "KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED"
    CONFIG_CODE_VARIABLE
        PACKAGE_SETUP_KWINEFFECTS_AUTOMOC_VARIABLES
)

# QindaQt: no handbook and no translation catalogs; both would install
# stock-named files (container-wm ADR-0289).

add_subdirectory(data)
# QindaQt: native helpers that are not edits of upstream files (qindaqt/).
# AGENT-CONTRACT: killer-helper (src/helpers/killer) runs terminate-process
# through pkexec; the polkit action names the same path.
set(QINDAQT_KWIN_TERMINATE_PROCESS_BIN "${KDE_INSTALL_FULL_LIBEXECDIR}/qindaqt-kwin/terminate-process")
set(QINDAQT_KWIN_PKEXEC "/usr/bin/pkexec" CACHE FILEPATH "pkexec, which runs qindaqt-kwin's privileged terminate-process helper")
option(QINDAQT_KWIN_BUILD_TESTS "Build the tests of qindaqt-kwin's own helpers without KWin's autotests" OFF)
add_subdirectory(qindaqt)
add_subdirectory(src)
add_subdirectory(qindaqt/session-lock)
add_subdirectory(qindaqt/capture-authority)

if (BUILD_TESTING)
    if (Qt6WaylandClient_VERSION VERSION_GREATER_EQUAL "6.10.0")
        find_package(Qt6WaylandClientPrivate ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE)
    endif()

    add_subdirectory(autotests)
    add_subdirectory(tests)
endif()

# add clang-format target for all our real source files
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

set(CMAKECONFIG_DBUS_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/QindaQtKWinDBusInterface")
configure_package_config_file(KWinDBusInterfaceConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinDBusInterfaceConfig.cmake"
    PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
    INSTALL_DESTINATION ${CMAKECONFIG_DBUS_INSTALL_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinDBusInterfaceConfig.cmake
    DESTINATION ${CMAKECONFIG_DBUS_INSTALL_DIR})

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/QindaQtKWin")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/KWinConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinConfig.cmake"
    INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

install(
    FILES
        cmake/modules/FindLibdrm.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QindaQtKWinConfigVersion.cmake"
    DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
    COMPONENT Devel
)

install(EXPORT KWinTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE QindaQtKWinTargets.cmake NAMESPACE QindaQtKWin:: )

# QindaQt: the QindaQt session supervisor starts the compositor; no systemd unit.
