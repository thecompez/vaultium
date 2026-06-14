import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Themed single-line text input with focus ring.
TextField {
    id: control

    implicitHeight: 40
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    color: Theme.fg
    placeholderTextColor: Theme.fgSubtle
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsBase

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bg
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.borderStrong
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }
}
