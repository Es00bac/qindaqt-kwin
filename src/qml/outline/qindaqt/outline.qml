/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import org.qindaqt.kwin
import org.kde.kirigami as Kirigami

Window {
    id: window

    readonly property int animationDuration: Kirigami.Units.longDuration
    property bool animationEnabled: false
    flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint
    color: "transparent"
    x: outline.unifiedGeometry.x
    y: outline.unifiedGeometry.y
    width: outline.unifiedGeometry.width
    height: outline.unifiedGeometry.height
    visible: outline.active

    onVisibleChanged: {
        if (visible) {
            animationEnabled = false
            frame.setGeometry(outline.visualParentGeometry.width > 0 && outline.visualParentGeometry.height > 0
                ? outline.visualParentGeometry : outline.geometry)
            animationEnabled = true
            if (outline.visualParentGeometry.width > 0 && outline.visualParentGeometry.height > 0) {
                frame.setGeometry(outline.geometry)
            }
        }
    }

    Connections {
        target: outline
        function onUnifiedGeometryChanged() {
            if (window.visible) {
                window.animationEnabled = false
                frame.setGeometry(outline.geometry)
                window.animationEnabled = true
            }
        }
    }

    Item {
        id: frame
        function setGeometry(geometry) {
            x = geometry.x - outline.unifiedGeometry.x
            y = geometry.y - outline.unifiedGeometry.y
            width = geometry.width
            height = geometry.height
        }
        Behavior on x { NumberAnimation { duration: window.animationDuration; easing.type: Easing.InOutCubic } enabled: window.animationEnabled }
        Behavior on y { NumberAnimation { duration: window.animationDuration; easing.type: Easing.InOutCubic } enabled: window.animationEnabled }
        Behavior on width { NumberAnimation { duration: window.animationDuration; easing.type: Easing.InOutCubic } enabled: window.animationEnabled }
        Behavior on height { NumberAnimation { duration: window.animationDuration; easing.type: Easing.InOutCubic } enabled: window.animationEnabled }

        readonly property rect maxArea: Workspace.clientArea(Workspace.MaximizeArea, Workspace.screenAt(Qt.point(outline.geometry.x, outline.geometry.y)), Workspace.currentDesktop)
        readonly property bool edgeLeft: outline.geometry.x !== maxArea.x
        readonly property bool edgeRight: outline.geometry.x + outline.geometry.width !== maxArea.x + maxArea.width
        readonly property bool edgeTop: outline.geometry.y !== maxArea.y
        readonly property bool edgeBottom: outline.geometry.y + outline.geometry.height !== maxArea.y + maxArea.height

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.highlightColor
            opacity: 0.12
        }
        Rectangle { x: 0; y: 0; width: parent.width; height: 2; color: Kirigami.Theme.highlightColor; visible: parent.edgeTop }
        Rectangle { x: 0; y: parent.height - height; width: parent.width; height: 2; color: Kirigami.Theme.highlightColor; visible: parent.edgeBottom }
        Rectangle { x: 0; y: 0; width: 2; height: parent.height; color: Kirigami.Theme.highlightColor; visible: parent.edgeLeft }
        Rectangle { x: parent.width - width; y: 0; width: 2; height: parent.height; color: Kirigami.Theme.highlightColor; visible: parent.edgeRight }
    }
}
