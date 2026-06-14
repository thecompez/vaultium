import QtQuick
import QtQuick.Layouts
import VaultiumUI

// Centered empty/zero-data state: icon, headline, supporting text, optional CTA.
ColumnLayout {
    id: root
    property string iconName: "info"
    property string title: ""
    property string body: ""
    property string actionText: ""
    signal actionClicked()

    spacing: Theme.space3

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 64; implicitHeight: 64
        radius: Theme.radiusLg
        color: Theme.muted
        AppIcon { anchors.centerIn: parent; name: root.iconName; color: Theme.fgSubtle; size: 30 }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.title
        color: Theme.fg
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fsMd
        font.weight: Font.DemiBold
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 420
        visible: root.body !== ""
        text: root.body
        color: Theme.fgMuted
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fsSm
    }
    AppButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.space2
        visible: root.actionText !== ""
        text: root.actionText
        iconName: "create"
        onClicked: root.actionClicked()
    }
}
