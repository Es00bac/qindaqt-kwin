// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "waylandwindow.h"
#include <QPointer>
namespace KWin
{
class SessionLockSurfaceV1Interface;
class SessionLockController;
class SessionLockWindow : public WaylandWindow
{
    Q_OBJECT
public:
    SessionLockWindow(SessionLockSurfaceV1Interface *role, LogicalOutput *output, SessionLockController *controller);
    WindowType windowType() const override
    {
        return WindowType::Normal;
    }
    bool isLockScreen() const override
    {
        return true;
    }
    bool isCloseable() const override
    {
        return false;
    }
    bool isMovable() const override
    {
        return false;
    }
    bool isMovableAcrossScreens() const override
    {
        return false;
    }
    bool isResizable() const override
    {
        return false;
    }
    bool isPlaceable() const override
    {
        return false;
    }
    bool wantsInput() const override;
    void destroyWindow() override;
    void closeWindow() override
    {
    }

protected:
    bool acceptsFocus() const override
    {
        return wantsInput();
    }
    Layer belongsToLayer() const override
    {
        return OverlayLayer;
    }
    void moveResizeInternal(const RectF &, MoveResizeMode) override
    {
    }

private:
    void committed();
    void configureOutput();
    SessionLockSurfaceV1Interface *m_role;
    QPointer<LogicalOutput> m_lockOutput;
    SessionLockController *m_controller;
};
}
