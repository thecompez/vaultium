pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import VaultiumUI

// A pill-style segmented selector. Set `options` (list of strings); read/bind
// `currentIndex`. Emits `activated(index)` on user choice.
Rectangle {
    id: root
    property var options: []
    property int currentIndex: 0
    signal activated(int index)

    implicitHeight: 36
    implicitWidth: row.implicitWidth + Theme.space1 * 2
    radius: Theme.radiusSm
    color: Theme.muted

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.space1
        spacing: Theme.space1

        Repeater {
            model: root.options
            delegate: Rectangle {
                required property int index
                required property var modelData
                readonly property bool selected: root.currentIndex === index

                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + Theme.space4 * 2
                radius: Theme.radiusSm - 2
                color: selected ? Theme.surface : "transparent"
                border.width: selected ? 1 : 0
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: parent.selected ? Theme.fg : Theme.fgMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsSm
                    font.weight: parent.selected ? Font.DemiBold : Font.Medium
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.currentIndex = index; root.activated(index); }
                }
            }
        }
    }
}
