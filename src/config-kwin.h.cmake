#pragma once
#include <QLatin1String>

// QindaQt: plugin interface ids carry the exact fork release, e.g. 6.6.6.1.
#define KWIN_PLUGIN_VERSION_STRING "${QINDAQT_KWIN_VERSION}"
#define PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH}

#cmakedefine01 KWIN_BUILD_DECORATIONS
#cmakedefine01 KWIN_BUILD_KCMS
#cmakedefine01 KWIN_BUILD_NOTIFICATIONS
#cmakedefine01 KWIN_BUILD_SCREENLOCKER
#cmakedefine01 KWIN_BUILD_TABBOX
#define KWIN_BUILD_ACTIVITIES 0
#cmakedefine01 KWIN_BUILD_GLOBALSHORTCUTS
#cmakedefine01 KWIN_NATIVE_SHORTCUTS
#cmakedefine01 KWIN_BUILD_X11
#cmakedefine01 KWIN_BUILD_QACCESSIBILITYCLIENT
constexpr QLatin1String KWIN_CONFIG("qindaqt/kwinrc");
// QindaQt: report the exact fork release (e.g. 6.6.6.1) in --version and
// supportInformation; container-wm's release contract checks it.
constexpr QLatin1String KWIN_VERSION_STRING("${QINDAQT_KWIN_VERSION}");
constexpr QLatin1String XCB_VERSION_STRING("${XCB_VERSION}");
constexpr QLatin1String KWIN_KILLER_BIN("${KWIN_KILLER_BIN}");
constexpr QLatin1String LIBEXEC_DIR("${CMAKE_INSTALL_FULL_LIBEXECDIR}");
#cmakedefine01 HAVE_X11_XCB
#cmakedefine01 HAVE_X11_XINPUT
#cmakedefine01 HAVE_GBM_BO_GET_FD_FOR_PLANE
#cmakedefine01 HAVE_GBM_BO_CREATE_WITH_MODIFIERS2
#cmakedefine01 HAVE_MEMFD
#cmakedefine01 HAVE_SCHED_RESET_ON_FORK
#cmakedefine01 HAVE_XKBCOMMON_NO_SECURE_GETENV
#cmakedefine01 HAVE_XWAYLAND_ENABLE_EI_PORTAL
#cmakedefine01 HAVE_DL_LIBRARY
#cmakedefine01 HAVE_LIBDRM_FAUX

constexpr QLatin1String XWAYLAND_SESSION_SCRIPTS("${XWAYLAND_SESSION_SCRIPTS}");
#cmakedefine01 HAVE_WAYLAND_PROTOCOLS_147
