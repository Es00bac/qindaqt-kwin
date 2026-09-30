/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

class QDBusServiceWatcher;
class QTimer;

namespace KWin {

class NightLightScheduleClient final : public QObject
{
    Q_OBJECT
public:
    explicit NightLightScheduleClient(QObject *parent = nullptr);
    ~NightLightScheduleClient() override;
    void start();
    void stop();

Q_SIGNALS:
    void frameReceived(const QVariantMap &values, qulonglong revision);
    void unavailable(const QString &reason);

private Q_SLOTS:
    void receiveFrame(const QByteArray &nonce, qulonglong cookie,
                      qulonglong revision, const QVariantMap &values);

private:
    void resolveOwner();
    void bindOwner(const QString &owner);
    void subscribe();
    void refuse(const QString &reason);

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_receiptTimeout = nullptr;
    QString m_owner;
    QByteArray m_nonce;
    qulonglong m_cookie = 0;
    qulonglong m_revision = 0;
    quint64 m_generation = 0;
    bool m_started = false;
};

} // namespace KWin
