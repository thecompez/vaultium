import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

// Live transfer/operation progress strip. Visible while a backup runs; shows a
// real progress bar (with speed + ETA) during remote downloads, an indeterminate
// bar otherwise, plus a Cancel button.
Rectangle {
    id: bar
    property var controller

    readonly property string state: controller ? controller.transferState : "idle"
    readonly property real pct: controller ? controller.transferPercent : 0
    readonly property bool active: controller && controller.transferActive

    function label(s) {
        if (s === "cleaningup") return qsTr("Cleaning up");
        return s.length ? (s.charAt(0).toUpperCase() + s.slice(1)) : qsTr("Working");
    }
    function tone(s) {
        if (s === "completed") return "success";
        if (s === "failed" || s === "cancelled") return "danger";
        return "info";
    }

    visible: active
    implicitHeight: visible ? 60 : 0
    color: Theme.surfaceAlt
    Behavior on implicitHeight { NumberAnimation { duration: Theme.durFast } }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space6
        anchors.rightMargin: Theme.space5
        spacing: Theme.space4

        StatusPill { tone: bar.tone(bar.state); text: bar.label(bar.state) }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3
                Text {
                    visible: bar.controller && bar.controller.transferFile.length > 0
                    text: bar.controller ? bar.controller.transferFile : ""
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsXs
                    font.weight: Font.Medium
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 280
                }
                Text {
                    text: bar.controller ? bar.controller.transferDetail : ""
                    color: Theme.fgMuted
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fsXs
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    visible: bar.controller && bar.controller.transferSpeed.length > 0
                    text: bar.controller ? bar.controller.transferSpeed : ""
                    color: Theme.fgMuted; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs
                }
                Text {
                    visible: bar.controller && bar.controller.transferEta.length > 0
                    text: bar.controller ? (qsTr("ETA ") + bar.controller.transferEta) : ""
                    color: Theme.fgSubtle; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs
                }
                Text {
                    visible: bar.pct > 0
                    text: Math.round(bar.pct * 100) + "%"
                    color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; font.weight: Font.DemiBold
                }
            }

            // Progress track.
            Rectangle {
                id: track
                Layout.fillWidth: true
                implicitHeight: 6
                radius: 3
                color: Theme.muted
                clip: true

                // Determinate fill.
                Rectangle {
                    visible: bar.pct > 0
                    height: parent.height
                    radius: parent.radius
                    width: parent.width * Math.min(1, bar.pct)
                    color: bar.state === "failed" || bar.state === "cancelled" ? Theme.danger : Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.durFast } }
                }

                // Indeterminate sweep (when no byte total is known).
                Rectangle {
                    id: sweep
                    visible: bar.active && bar.pct <= 0
                    height: parent.height
                    radius: parent.radius
                    width: parent.width * 0.3
                    color: Theme.accent
                    opacity: 0.8
                    XAnimator on x {
                        from: -track.width * 0.3
                        to: track.width
                        duration: 1100
                        loops: Animation.Infinite
                        running: sweep.visible
                    }
                }
            }
        }

        AppButton {
            text: qsTr("Cancel")
            iconName: "close"
            variant: "danger"
            implicitHeight: 32
            onClicked: if (bar.controller) bar.controller.cancel()
        }
    }
}
