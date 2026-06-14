import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Themed on/off switch.
Switch {
    id: control
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsBase

    indicator: Rectangle {
        implicitWidth: 42
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.secondary
        border.color: control.checked ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18; height: 18
            radius: 9
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Text {
        text: control.text
        color: Theme.fg
        font: control.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + Theme.space2
    }
}
