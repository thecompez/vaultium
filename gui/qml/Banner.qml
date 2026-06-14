import QtQuick
import QtQuick.Layouts
import VaultiumUI

// Inline contextual message. tone: info | success | warning | danger.
Rectangle {
    id: root
    property string tone: "info"
    property string text: ""
    property string iconName: tone === "danger" ? "alert"
        : tone === "warning" ? "alert"
        : tone === "success" ? "check-circle" : "info"

    readonly property color _fg: tone === "success" ? Theme.accent
        : tone === "warning" ? Theme.warning
        : tone === "danger" ? Theme.danger : Theme.info
    readonly property color _bg: tone === "success" ? Theme.accentSoft
        : tone === "warning" ? Theme.warningSoft
        : tone === "danger" ? Theme.dangerSoft : Theme.infoSoft

    implicitHeight: row.implicitHeight + Theme.space3 * 2
    radius: Theme.radiusSm
    color: _bg

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.space3
        spacing: Theme.space3

        AppIcon { name: root.iconName; color: root._fg; size: 18; Layout.alignment: Qt.AlignTop }
        Text {
            Layout.fillWidth: true
            text: root.text
            color: root._fg
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsSm
        }
    }
}
