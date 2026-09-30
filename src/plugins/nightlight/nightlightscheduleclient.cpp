/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "nightlightscheduleclient.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QTime>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

#include <limits>
#include <cmath>
#include <QRegularExpression>

namespace KWin {
namespace {
const QString Service = QStringLiteral("org.qindaqt.NightLight");
const QString Path = QStringLiteral("/org/qindaqt/NightLight");
const QString Interface = QStringLiteral("org.qindaqt.NightLight.Schedule1");
const QString Signal = QStringLiteral("ScheduleFrame");
const QString Bus = QStringLiteral("org.freedesktop.DBus");
const QString BusPath = QStringLiteral("/org/freedesktop/DBus");
const QString BusInterface = QStringLiteral("org.freedesktop.DBus");
constexpr int TimeoutMs = 2000;

bool field(const QVariantMap &values, const char *key, int type, QVariant *out)
{
    const auto it = values.constFind(QString::fromLatin1(key));
    if (it == values.cend() || it->metaType().id() != type) return false;
    *out = *it;
    return true;
}

bool validFrame(const QVariantMap &values)
{
    if (values.size() != 21) return false;
    QVariant v;
    bool active = false;
    QString mode;
    int day = 0, night = 0, transition = 0;
    QStringList disabled;
    QString source, sunrise, sunset;
    bool automatic = false, available = false, daylight = false;
    double latitude = 0, longitude = 0;
    if (!field(values, "active", QMetaType::Bool, &v)) return false;
    active = v.toBool();
    if (!field(values, "mode", QMetaType::QString, &v)) return false;
    mode = v.toString();
    if (mode != QLatin1String("Constant") && mode != QLatin1String("DarkLight")) return false;
    if (!field(values, "dayTemperatureKelvin", QMetaType::Int, &v)) return false;
    day = v.toInt();
    if (!field(values, "nightTemperatureKelvin", QMetaType::Int, &v)) return false;
    night = v.toInt();
    if (day < 1000 || day > 6500 || day % 100 || night < 1000 || night > 6500 || night % 100) return false;
    if (!field(values, "disabledOutputs", QMetaType::QStringList, &v)) return false;
    disabled = v.toStringList();
    if (!field(values, "disabledOutputUuids", QMetaType::QStringList, &v)) return false;
    const QStringList disabledUuids = v.toStringList();
    if (disabledUuids.size() > 32) return false;
    for (qsizetype i = 0; i < disabledUuids.size(); ++i) {
        if (disabledUuids.at(i).isEmpty() || disabledUuids.at(i).toUtf8().size() > 128
            || disabledUuids.indexOf(disabledUuids.at(i)) != i) return false;
    }
    if (!field(values, "outputIdentityAvailable", QMetaType::Bool, &v)) return false;
    const bool identitiesAvailable = v.toBool();
    if (identitiesAvailable
            ? disabledUuids.size() != disabled.size()
            : !disabledUuids.isEmpty()) return false;
    if (disabled.size() > 32) return false;
    for (qsizetype i = 0; i < disabled.size(); ++i) {
        const QByteArray id = disabled.at(i).toUtf8();
        static const QRegularExpression stableId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,109}(?:#[1-9][0-9]{0,2})?$") );
        if (id.size() > 128 || !stableId.match(disabled.at(i)).hasMatch()
            || disabled.indexOf(disabled.at(i)) != i) return false;
    }
    if (!field(values, "scheduleSource", QMetaType::QString, &v)) return false;
    source = v.toString();
    if (source != QLatin1String("Location") && source != QLatin1String("Times")) return false;
    if (!field(values, "automaticLocation", QMetaType::Bool, &v)) return false;
    automatic = v.toBool();
    if (!field(values, "latitudeDegrees", QMetaType::Double, &v)) return false;
    latitude = v.toDouble();
    if (!field(values, "longitudeDegrees", QMetaType::Double, &v)) return false;
    longitude = v.toDouble();
    if (!std::isfinite(latitude) || latitude < -90 || latitude > 90
        || !std::isfinite(longitude) || longitude < -180 || longitude > 180) return false;
    if (!field(values, "sunriseStart", QMetaType::QString, &v)) return false;
    sunrise = v.toString();
    if (!QTime::fromString(sunrise, QStringLiteral("HH:mm:ss")).isValid()) return false;
    if (!field(values, "sunsetStart", QMetaType::QString, &v)) return false;
    sunset = v.toString();
    if (!QTime::fromString(sunset, QStringLiteral("HH:mm:ss")).isValid()) return false;
    if (!field(values, "transitionSeconds", QMetaType::Int, &v)) return false;
    transition = v.toInt();
    if (transition < 60 || transition > 7200) return false;
    if (!field(values, "available", QMetaType::Bool, &v)) return false;
    available = v.toBool();
    if (!field(values, "daylight", QMetaType::Bool, &v)) return false;
    daylight = v.toBool();
    Q_UNUSED(automatic);
    Q_UNUSED(daylight);
    const bool hasSchedule = active && mode == QLatin1String("DarkLight");
    for (const char *name : {"previousStartMs", "previousEndMs", "nextStartMs", "nextEndMs"}) {
        if (!field(values, name, QMetaType::ULongLong, &v)) return false;
        if (available && hasSchedule && v.toULongLong() == 0) return false;
        if (v.toULongLong() > quint64(std::numeric_limits<qint64>::max())) return false;
    }
    if (available && hasSchedule) {
        const auto prevStart = values.value(QStringLiteral("previousStartMs")).toULongLong();
        const auto prevEnd = values.value(QStringLiteral("previousEndMs")).toULongLong();
        const auto nextStart = values.value(QStringLiteral("nextStartMs")).toULongLong();
        const auto nextEnd = values.value(QStringLiteral("nextEndMs")).toULongLong();
        if (prevStart >= prevEnd || nextStart >= nextEnd) return false;
    }
    if (!field(values, "diagnostic", QMetaType::QString, &v)
        || v.toString().toUtf8().size() > 512) return false;
    return true;
}
} // namespace

NightLightScheduleClient::NightLightScheduleClient(QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::sessionBus())
{
    m_receiptTimeout = new QTimer(this);
    m_receiptTimeout->setSingleShot(true);
    m_receiptTimeout->setInterval(TimeoutMs);
    connect(m_receiptTimeout, &QTimer::timeout, this, [this] {
        refuse(QStringLiteral("schedule authority did not confirm its owner-bound subscription"));
    });
    m_watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &NightLightScheduleClient::resolveOwner);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this] { bindOwner({}); });
}

NightLightScheduleClient::~NightLightScheduleClient() { stop(); }
void NightLightScheduleClient::start()
{
    if (m_started) return;
    m_started = true;
    resolveOwner();
    QDBusMessage activate = QDBusMessage::createMethodCall(Bus, BusPath, BusInterface,
                                                            QStringLiteral("StartServiceByName"));
    activate.setArguments({QVariant(Service), QVariant(uint(0))});
    m_bus.asyncCall(activate, TimeoutMs);
}
void NightLightScheduleClient::stop()
{
    if (!m_started) return;
    m_started = false;
    ++m_generation;
    m_receiptTimeout->stop();
    if (!m_owner.isEmpty()) {
        m_bus.disconnect(m_owner, Path, Interface, Signal, this,
            SLOT(receiveFrame(QByteArray,qulonglong,qulonglong,QVariantMap)));
        if (m_cookie) {
            QDBusMessage call = QDBusMessage::createMethodCall(m_owner, Path, Interface,
                                                               QStringLiteral("Unsubscribe"));
            call.setArguments({QVariant(m_nonce), QVariant::fromValue(m_cookie)});
            m_bus.asyncCall(call, TimeoutMs);
        }
    }
    m_owner.clear(); m_nonce.clear(); m_cookie = 0; m_revision = 0;
}
void NightLightScheduleClient::resolveOwner()
{
    if (!m_started) return;
    const quint64 generation = ++m_generation;
    QDBusMessage call = QDBusMessage::createMethodCall(Bus, BusPath, BusInterface,
                                                       QStringLiteral("GetNameOwner"));
    call.setArguments({QVariant(Service)});
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, TimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        if (!m_started || generation != m_generation) return;
        bindOwner(reply.isError() ? QString{} : reply.value());
    });
}
void NightLightScheduleClient::bindOwner(const QString &owner)
{
    if (owner == m_owner) return;
    if (!m_owner.isEmpty()) {
        m_bus.disconnect(m_owner, Path, Interface, Signal, this,
            SLOT(receiveFrame(QByteArray,qulonglong,qulonglong,QVariantMap)));
        if (m_cookie) {
            QDBusMessage call = QDBusMessage::createMethodCall(m_owner, Path, Interface,
                                                               QStringLiteral("Unsubscribe"));
            call.setArguments({QVariant(m_nonce), QVariant::fromValue(m_cookie)});
            m_bus.asyncCall(call, TimeoutMs);
        }
    }
    m_owner.clear(); m_nonce.clear(); m_cookie = 0; m_revision = 0;
    m_receiptTimeout->stop(); ++m_generation;
    if (!owner.isEmpty() && m_started) {
        m_owner = owner;
        m_nonce = QUuid::createUuid().toRfc4122();
        if (m_nonce.size() != 16 || !m_bus.connect(m_owner, Path, Interface, Signal, this,
                SLOT(receiveFrame(QByteArray,qulonglong,qulonglong,QVariantMap)))) {
            m_owner.clear();
            refuse(QStringLiteral("cannot bind the schedule signal to its unique owner"));
            return;
        }
        subscribe();
    } else if (m_started) {
        refuse(QStringLiteral("schedule authority is unavailable"));
    }
}
void NightLightScheduleClient::subscribe()
{
    if (!m_started || m_owner.isEmpty()) return;
    const quint64 generation = m_generation;
    QDBusMessage call = QDBusMessage::createMethodCall(m_owner, Path, Interface,
                                                       QStringLiteral("Subscribe"));
    call.setArguments({QVariant(m_nonce)});
    m_receiptTimeout->start();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, TimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<> reply = *finished;
        finished->deleteLater();
        if (!m_started || generation != m_generation || !reply.isError()) return;
        m_receiptTimeout->stop();
        refuse(QStringLiteral("schedule authority refused its subscription"));
    });
}
void NightLightScheduleClient::receiveFrame(const QByteArray &nonce, qulonglong cookie,
                                           qulonglong revision, const QVariantMap &values)
{
    if (!m_started || m_owner.isEmpty() || nonce != m_nonce || cookie == 0
        || revision == 0 || (m_cookie != 0 && cookie != m_cookie) || revision <= m_revision) return;
    if (!validFrame(values)) {
        refuse(QStringLiteral("schedule authority sent an invalid frame"));
        return;
    }
    m_cookie = cookie; m_revision = revision; m_receiptTimeout->stop();
    Q_EMIT frameReceived(values, revision);
}
void NightLightScheduleClient::refuse(const QString &reason)
{
    m_receiptTimeout->stop();
    Q_EMIT unavailable(reason);
}

} // namespace KWin
