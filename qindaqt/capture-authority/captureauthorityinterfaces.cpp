// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"

#include "wayland/clientconnection.h"

namespace KWin
{
bool CaptureAuthority::isTaggedCaptureClient(ClientConnection *client) const
{
    if (!client) {
        return false;
    }
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (const auto tag = m_testTags.constFind(client); tag != m_testTags.cend()) {
        return !tag->gui;
    }
#endif
    return m_captureTags.contains(client);
}

bool CaptureAuthority::isOwnedGuiClient(ClientConnection *client) const
{
    if (!client) {
        return false;
    }
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (const auto tag = m_testTags.constFind(client); tag != m_testTags.cend()) {
        return tag->gui;
    }
#endif
    return m_guiTags.contains(client);
}

bool CaptureAuthority::guiClientAllowed(ClientConnection *client) const
{
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (const auto tag = m_testTags.constFind(client); tag != m_testTags.cend()) {
        return tag->gui && privacyAllows();
    }
#endif
    const auto found = m_guiTags.constFind(client);
    if (found == m_guiTags.cend()) {
        return false;
    }
    const auto job = m_jobs.find(*found);
    return job != m_jobs.end() && jobLive(*job->second);
}

bool CaptureAuthority::allowTaggedInterface(ClientConnection *client, const QByteArray &name) const
{
    std::optional<CaptureWire::Public::Scope> scope;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (const auto tag = m_testTags.constFind(client); tag != m_testTags.cend()) {
        if (tag->gui || !privacyAllows()) {
            return false;
        }
        scope = tag->scope;
    }
#endif
    if (!scope) {
        const auto found = m_captureTags.constFind(client);
        if (found == m_captureTags.cend()) {
            return false;
        }
        const auto it = m_jobs.find(*found);
        if (it == m_jobs.end() || !jobLive(*it->second)) {
            return false;
        }
        scope = it->second->scope;
    }
    if (name == QByteArrayLiteral("wl_output") || name == QByteArrayLiteral("zxdg_output_manager_v1")) {
        return true;
    }
    return *scope == CaptureWire::Public::Scope::ScreenCast
        && name == QByteArrayLiteral("zkde_screencast_unstable_v1");
}

std::optional<quint64> CaptureAuthority::streamJob(ClientConnection *client) const
{
    const auto found = m_captureTags.constFind(client);
    if (found == m_captureTags.cend()) {
        return std::nullopt;
    }
    return *found;
}

bool CaptureAuthority::streamAllowed(quint64 id) const
{
    const auto it = m_jobs.find(id);
    return it != m_jobs.end() && it->second->scope == CaptureWire::Public::Scope::ScreenCast
        && it->second->captureReady && jobLive(*it->second);
}

std::optional<quint64> CaptureAuthority::beginScreenshot(const QString &sender, pid_t pid)
{
    for (const auto &[id, owned] : m_jobs) {
        Job &job = *owned;
        if (job.scope == CaptureWire::Public::Scope::Screenshot && job.captureReady
            && !job.screenshotConsumed && job.helperSender == sender
            && job.helper->pid() == pid && jobLive(job)) {
            job.screenshotConsumed = true;
            return id;
        }
    }
    return std::nullopt;
}

bool CaptureAuthority::screenshotOperationAllowed(quint64 id, const QString &sender, pid_t pid) const
{
    const auto it = m_jobs.find(id);
    return it != m_jobs.end() && it->second->scope == CaptureWire::Public::Scope::Screenshot
        && it->second->captureReady && it->second->screenshotConsumed
        && it->second->helperSender == sender && it->second->helper->pid() == pid
        && jobLive(*it->second);
}

bool CaptureAuthority::isProtectedHelperSender(const QString &sender, pid_t pid) const
{
    return m_helperSenders.value(sender, 0) == pid && pid > 0;
}

#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
void CaptureAuthority::tagTestClient(ClientConnection *client, CaptureWire::Public::Scope scope, bool gui)
{
    Q_ASSERT(client && !m_captureTags.contains(client) && !m_guiTags.contains(client));
    Q_ASSERT(scope == CaptureWire::Public::Scope::Screenshot
             || scope == CaptureWire::Public::Scope::ScreenCast);
    m_testTags.insert(client, {scope, gui});
    connect(client, &QObject::destroyed, this, [this, client] { m_testTags.remove(client); });
}
#endif
}
