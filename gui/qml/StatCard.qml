import QtQuick
import QtQuick.Layouts
import VaultiumUI

// Dashboard metric card: tinted icon chip, value, label.
Card {
    id: root
    property string statIcon: "info"
    property string statValue: ""
    property string statLabel: ""
    property color statColor: Theme.accent

    RowLayout {
        anchors.fill: parent
        spacing: Theme.space4

        Rectangle {
            implicitWidth: 44; implicitHeight: 44
            radius: Theme.radiusSm
            color: Qt.rgba(root.statColor.r, root.statColor.g, root.statColor.b, 0.14)
            AppIcon { anchors.centerIn: parent; name: root.statIcon; color: root.statColor; size: 22 }
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text {
                text: root.statValue
                color: Theme.fg
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsLg
                font.weight: Font.Bold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.statLabel
                color: Theme.fgMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
