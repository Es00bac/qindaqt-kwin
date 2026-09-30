/*
    SPDX-FileCopyrightText: 2016 Martin Graesslin <mgraesslin@kde.org>
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: window

    // AGENT-GUARD: KWin renders this as an InternalWindow. Keep input
    // transparency and focus refusal: Plasma Dialog's outputOnly OSD role
    // had the same passive interaction contract.
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    visible: osd.visible
    color: "transparent"
    width: content.implicitWidth + Kirigami.Units.largeSpacing * 2
    height: content.implicitHeight + Kirigami.Units.largeSpacing * 2
    x: screen ? screen.virtualX + (screen.width - width) / 2 : 0
    y: screen ? screen.virtualY + (screen.height - height) / 3 : 0

    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        opacity: 0.92
        border.color: Kirigami.Theme.textColor
        border.width: 1
    }

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            implicitWidth: Kirigami.Units.iconSizes.medium
            implicitHeight: implicitWidth
            source: osd.iconName
            visible: osd.iconName !== ""
        }

        Text {
            text: osd.message
            color: Kirigami.Theme.textColor
            font: Kirigami.Theme.defaultFont
        }
    }
}
