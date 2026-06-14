import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Square, borderless icon-only button with a subtle hover fill.
Button {
    id: control

    property string iconName: ""
    property int iconSize: 18
    property color iconColor: Theme.fgMuted
    property string tip: ""

    ToolTip {
        visible: control.hovered && control.tip.length > 0
        text: control.tip
        delay: 450
        background: Rectangle { radius: Theme.radiusSm; color: Theme.surfaceAlt; border.color: Theme.borderStrong; border.width: 1 }
        contentItem: Text { text: control.tip; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
    }

    implicitWidth: 38
    implicitHeight: 38
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.iconColor
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.down ? Theme.secondary
            : control.hovered ? Theme.muted : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
