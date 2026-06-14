pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

// A multi-step wizard frame: title bar with Cancel, a left step rail, a content
// area (place a StackLayout bound to `currentStep` inside), and a Back/Next footer.
// Navigation is driven by the host via the back()/next()/cancel() signals so each
// step can gate advancement with `canAdvance`.
Rectangle {
    id: root
    property var steps: []
    property var stepIcons: []
    property var stepDescriptions: []
    property int currentStep: 0
    property string title: ""
    property bool canAdvance: true
    property bool busy: false
    readonly property bool onLastStep: currentStep >= steps.length - 1

    onCurrentStepChanged: contentFade.restart()

    default property alias content: contentHolder.data

    signal back()
    signal next()
    signal cancel()

    color: Theme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: Theme.surfaceAlt
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space6
                anchors.rightMargin: Theme.space5
                spacing: Theme.space3
                Text {
                    text: root.title
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsLg
                    font.weight: Font.Bold
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "close"
                    tip: qsTr("Cancel")
                    onClicked: root.cancel()
                }
            }
        }

        // Body: step rail + content
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Step rail
            Rectangle {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                color: Theme.surfaceAlt
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space5
                    spacing: 0

                    Repeater {
                        model: root.steps
                        delegate: RowLayout {
                            id: stepRow
                            required property int index
                            required property var modelData
                            readonly property bool done: index < root.currentStep
                            readonly property bool current: index === root.currentStep
                            readonly property bool last: index === root.steps.length - 1
                            Layout.fillWidth: true
                            spacing: Theme.space3

                            // Indicator column: circle + connector to the next step.
                            ColumnLayout {
                                Layout.alignment: Qt.AlignTop
                                spacing: 0
                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 34; implicitHeight: 34
                                    radius: 17
                                    color: (stepRow.done || stepRow.current) ? Theme.accent : Theme.muted
                                    border.width: stepRow.current ? 4 : 0
                                    border.color: Theme.accentSoft
                                    Behavior on color { ColorAnimation { duration: Theme.durBase } }
                                    AppIcon {
                                        anchors.centerIn: parent
                                        visible: stepRow.done
                                        name: "check"; size: 16; color: Theme.textOnAccent
                                    }
                                    AppIcon {
                                        anchors.centerIn: parent
                                        visible: !stepRow.done
                                        name: (root.stepIcons && root.stepIcons[stepRow.index]) ? root.stepIcons[stepRow.index] : "dashboard"
                                        size: 16
                                        color: stepRow.current ? Theme.textOnAccent : Theme.fgMuted
                                    }
                                }
                                Rectangle {
                                    visible: !stepRow.last
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 2; implicitHeight: 38
                                    color: stepRow.done ? Theme.accent : Theme.border
                                    Behavior on color { ColorAnimation { duration: Theme.durBase } }
                                }
                            }

                            // Title + description.
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space1
                                Layout.alignment: Qt.AlignTop
                                spacing: 2
                                Text {
                                    text: stepRow.modelData
                                    color: (stepRow.current || stepRow.done) ? Theme.fg : Theme.fgMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fsBase
                                    font.weight: stepRow.current ? Font.DemiBold : Font.Medium
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    visible: root.stepDescriptions && root.stepDescriptions[stepRow.index] !== undefined
                                    text: (root.stepDescriptions && root.stepDescriptions[stepRow.index]) ? root.stepDescriptions[stepRow.index] : ""
                                    color: stepRow.current ? Theme.fgMuted : Theme.fgSubtle
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fsXs
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: Theme.space3
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // Content (fades in on each step change for a smooth transition)
            Item {
                id: contentHolder
                Layout.fillWidth: true
                Layout.fillHeight: true
                SequentialAnimation {
                    id: contentFade
                    NumberAnimation { target: contentHolder; property: "opacity"; from: 0.25; to: 1.0; duration: Theme.durBase; easing.type: Easing.OutCubic }
                }
            }
        }

        // Footer
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 72
            color: Theme.surfaceAlt
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space6
                anchors.rightMargin: Theme.space6
                spacing: Theme.space3
                AppButton {
                    text: qsTr("Back")
                    variant: "ghost"
                    visible: root.currentStep > 0
                    onClicked: root.back()
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: root.onLastStep ? qsTr("Finish") : qsTr("Next")
                    iconName: root.onLastStep ? "check" : ""
                    variant: "primary"
                    enabled: root.canAdvance && !root.busy
                    loading: root.busy
                    onClicked: root.next()
                }
            }
        }
    }
}
