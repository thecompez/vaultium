import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Themed dropdown.
ComboBox {
    id: control

    implicitHeight: 40
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsBase

    contentItem: Text {
        leftPadding: Theme.space3
        rightPadding: control.indicator.width + Theme.space2
        text: control.displayText
        color: Theme.fg
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bg
        border.width: control.activeFocus || control.popup.visible ? 2 : 1
        border.color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.borderStrong
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }

    indicator: AppIcon {
        x: control.width - width - Theme.space3
        y: control.topPadding + (control.availableHeight - height) / 2
        size: 16
        name: "chevron"
        color: Theme.fgMuted
    }

    delegate: ItemDelegate {
        id: itemDelegate
        width: ListView.view ? ListView.view.width : control.width
        required property int index
        required property var modelData
        highlighted: control.highlightedIndex === itemDelegate.index
        contentItem: Text {
            text: itemDelegate.modelData
            color: itemDelegate.highlighted ? Theme.accent : Theme.fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsBase
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: itemDelegate.highlighted ? Theme.muted : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: Theme.space1
        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.surfaceAlt
            border.color: Theme.borderStrong
            border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
