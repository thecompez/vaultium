pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

// Collapsible console docked at the bottom of the window. Streams the engine's
// log lines (via controller.logMessage), colored by level. Auto-expands on error.
Rectangle {
    id: root
    property var controller
    property bool expanded: false

    implicitHeight: 44 + (expanded ? 220 : 0)
    color: Theme.surfaceAlt
    Behavior on implicitHeight { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    function levelColor(line) {
        if (line.indexOf("[ERROR]") >= 0) return Theme.danger;
        if (line.indexOf("[SUCCESS]") >= 0) return Theme.accent;
        if (line.indexOf("[WARNING]") >= 0) return Theme.warning;
        if (line.indexOf("$ ") === 0) return Theme.info;
        return Theme.fgMuted;
    }

    ListModel { id: logModel }

    Connections {
        target: root.controller
        function onLogMessage(line) {
            logModel.append({ line: line });
            while (logModel.count > 1000) {
                logModel.remove(0);
            }
            if (line.indexOf("[ERROR]") >= 0) {
                root.expanded = true; // surface failures
            }
            list.positionViewAtEnd();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header bar
        Rectangle {
            id: header
            Layout.fillWidth: true
            implicitHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space5
                anchors.rightMargin: Theme.space4
                spacing: Theme.space3

                AppIcon { name: "info"; size: 16; color: Theme.fgMuted }
                Text {
                    text: qsTr("Console")
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsSm
                    font.weight: Font.DemiBold
                }
                StatusPill {
                    visible: root.controller && root.controller.busy
                    tone: "warning"
                    text: qsTr("Working")
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: logModel.count + qsTr(" lines")
                    color: Theme.fgSubtle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsXs
                }
                IconButton {
                    iconName: "copy"
                    tip: qsTr("Copy all log lines")
                    enabled: logModel.count > 0 && root.controller
                    onClicked: {
                        var text = "";
                        for (var i = 0; i < logModel.count; ++i) {
                            text += logModel.get(i).line + "\n";
                        }
                        root.controller.copyToClipboard(text);
                    }
                }
                IconButton {
                    iconName: "trash"
                    tip: qsTr("Clear log")
                    onClicked: logModel.clear()
                }
                IconButton {
                    iconName: "chevron"
                    rotation: root.expanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.durFast } }
                    tip: root.expanded ? qsTr("Hide console") : qsTr("Show console")
                    onClicked: root.expanded = !root.expanded
                }
            }
        }

        // Log lines
        ListView {
            id: list
            visible: root.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: logModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Text {
                required property string line
                width: ListView.view ? ListView.view.width : 0
                leftPadding: Theme.space5
                rightPadding: Theme.space4
                topPadding: 1
                bottomPadding: 1
                text: line
                color: root.levelColor(line)
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fsXs
                wrapMode: Text.WrapAnywhere
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: logModel.count === 0
                text: qsTr("Engine output will appear here.")
                color: Theme.fgSubtle
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsSm
            }
        }
    }
}
