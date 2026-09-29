// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessionlockwindow.h"
#include "core/output.h"
#include "sessionlock_v1.h"
#include "sessionlockcontroller.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "workspace.h"
namespace KWin
{
SessionLockWindow::SessionLockWindow(SessionLockSurfaceV1Interface *role, LogicalOutput *output, SessionLockController *controller)
    : WaylandWindow(role->surface())
    , m_role(role)
    , m_lockOutput(output)
    , m_controller(controller)
{
    setOutput(output);
    setMoveResizeOutput(output);
    setSkipSwitcher(true);
    setSkipPager(true);
    setSkipTaskbar(true);
    connect(role, &SessionLockSurfaceV1Interface::validCommit, this, &SessionLockWindow::committed);
    connect(role, &SessionLockSurfaceV1Interface::aboutToBeDestroyed, this, &SessionLockWindow::destroyWindow);
    connect(surface(), &SurfaceInterface::aboutToBeDestroyed, this, &SessionLockWindow::destroyWindow);
    connect(output, &LogicalOutput::geometryChanged, this, &SessionLockWindow::configureOutput);
    connect(workspace(), &Workspace::outputRemoved, this, [this](LogicalOutput *output) {
        if (output == m_lockOutput) {
            destroyWindow();
        }
    });
    configureOutput();
}
bool SessionLockWindow::wantsInput() const
{
    return !isDeleted() && m_controller->isLockSurface(surface());
}
void SessionLockWindow::configureOutput()
{
    if (isDeleted() || !m_lockOutput) {
        return;
    }
    const auto geometry = m_lockOutput->geometry();
    m_role->configure(geometry.size());
    if (readyForPainting()) {
        updateGeometry(geometry);
    }
    surface()->setPreferredBufferScale(m_lockOutput->scale());
}
void SessionLockWindow::committed()
{
    if (isDeleted() || !m_lockOutput) {
        return;
    }
    setTargetScale(m_lockOutput->scale());
    updateGeometry(m_lockOutput->geometry());
    markAsMapped();
    workspace()->activateWindow(this);
}
void SessionLockWindow::destroyWindow()
{
    if (isDeleted()) {
        return;
    }
    m_role->disconnect(this);
    if (surface()) {
        surface()->disconnect(this);
    }
    markAsDeleted();
    Q_EMIT closed();
    cleanTabBox();
    StackingUpdatesBlocker blocker(workspace());
    cleanGrouping();
    waylandServer()->removeWindow(this);
    unref();
}
}
