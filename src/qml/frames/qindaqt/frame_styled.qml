/*
    SPDX-FileCopyrightText: 2022 MBition GmbH
    SPDX-FileContributor: Kai Uwe Broulik <kai_uwe.broulik@mbition.io>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    property QtObject effectFrame: null

    implicitWidth: layout.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: layout.implicitHeight + Kirigami.Units.largeSpacing * 2

    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        opacity: root.effectFrame.frameOpacity
        border.color: Kirigami.Theme.textColor
        border.width: 1
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.preferredWidth: root.effectFrame.iconSize.width
            Layout.preferredHeight: root.effectFrame.iconSize.height
            Layout.alignment: Qt.AlignHCenter
            animated: root.effectFrame.crossFadeEnabled
            visible: valid
            source: root.effectFrame.icon
        }

        Text {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Kirigami.Theme.textColor
            font: root.effectFrame.font
            visible: text !== ""
            text: root.effectFrame.text
        }
    }
}
