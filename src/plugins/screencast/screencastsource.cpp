/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screencastsource.h"
#include "opengl/glframebuffer.h"
#include "wayland_server.h"
#include <QImage>

namespace KWin
{

ScreenCastSource::ScreenCastSource()
{
}

bool ScreenCastSource::renderLockFallback(QImage *target) const
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    target->fill(Qt::black);
    return true;
}
bool ScreenCastSource::renderLockFallback(GLFramebuffer *target) const
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    GLFramebuffer::pushFramebuffer(target);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLfloat clearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    GLboolean writeMask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, writeMask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
    glColorMask(writeMask[0], writeMask[1], writeMask[2], writeMask[3]);
    GLFramebuffer::popFramebuffer();
    return true;
}
} // namespace KWin

#include "moc_screencastsource.cpp"
