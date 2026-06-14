import QtQuick
import VaultiumUI

// Small status badge with a colored dot. tone: neutral | success | warning | danger | info
Item {
    id: pill
    property string text: ""
    property string tone: "neutral"

    readonly property color _fg: tone === "success" ? Theme.accent
        : tone === "warning" ? Theme.warning
        : tone === "danger" ? Theme.danger
        : tone === "info" ? Theme.info
        : Theme.fgMuted
    readonly property color _bg: tone === "success" ? Theme.accentSoft
        : tone === "warning" ? Theme.warningSoft
        : tone === "danger" ? Theme.dangerSoft
        : tone === "info" ? Theme.infoSoft
        : Theme.muted

    implicitWidth: row.implicitWidth + Theme.space3 * 2
    implicitHeight: 24

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: pill._bg
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space2

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 7; height: 7; radius: 4
            color: pill._fg
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.text
            color: pill._fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsXs
            font.weight: Font.DemiBold
        }
    }
}
