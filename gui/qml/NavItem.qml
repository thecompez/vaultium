import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Sidebar navigation entry: icon + label, with active indicator and hover.
ItemDelegate {
    id: control

    property string iconName: ""
    property bool active: false

    implicitHeight: 44
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.active ? Theme.muted
            : control.hovered ? (Theme.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
            : "transparent"

        Rectangle {
            visible: control.active
            width: 3
            radius: 2
            height: parent.height - Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            color: Theme.accent
        }

        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    contentItem: Row {
        spacing: Theme.space3
        leftPadding: Theme.space3

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 18
            color: control.active ? Theme.accent : Theme.fgMuted
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.active ? Theme.fg : Theme.fgMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsBase
            font.weight: control.active ? Font.DemiBold : Font.Medium
        }
    }
}
