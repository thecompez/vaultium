import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

// Transient notification anchored top-right. Call notify(success, message).
// When `controller.lastArtifact` is set after a success, shows Open / Reveal /
// Copy actions.
Popup {
    id: toast

    property bool success: true
    property string message: ""
    property var controller

    readonly property string artifact: controller ? controller.lastArtifact : ""
    readonly property bool hasActions: success && artifact.length > 0

    function notify(ok, text) {
        toast.success = ok;
        toast.message = text;
        toast.open();
        hideTimer.interval = toast.hasActions ? 9000 : 4500;
        hideTimer.restart();
    }

    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: Math.min(440, parent ? parent.width - Theme.space6 : 440)

    x: parent ? parent.width - width - Theme.space5 : 0
    y: Theme.space5

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durBase }
            NumberAnimation { property: "y"; from: 0; to: Theme.space5; duration: Theme.durBase; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durFast } }

    Timer { id: hideTimer; interval: 4500; onTriggered: toast.close() }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.borderStrong

        Rectangle {
            width: 4; radius: 2
            height: parent.height - Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.space2
            color: toast.success ? Theme.accent : Theme.danger
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.space3

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space4
            Layout.leftMargin: Theme.space5
            spacing: Theme.space3
            AppIcon {
                Layout.alignment: Qt.AlignTop
                name: toast.success ? "check-circle" : "alert"
                color: toast.success ? Theme.accent : Theme.danger
                size: 20
            }
            Text {
                Layout.fillWidth: true
                text: toast.message
                color: Theme.fg
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsBase
            }
        }

        RowLayout {
            visible: toast.hasActions
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space5
            Layout.rightMargin: Theme.space4
            Layout.bottomMargin: Theme.space4
            spacing: Theme.space2
            AppButton { text: qsTr("Open"); iconName: "play"; variant: "secondary"; onClicked: { toast.controller.openPath(toast.artifact); toast.close(); } }
            AppButton { text: qsTr("Reveal"); iconName: "folder"; variant: "ghost"; onClicked: { toast.controller.revealInFolder(toast.artifact); toast.close(); } }
            AppButton { text: qsTr("Copy path"); iconName: "copy"; variant: "ghost"; onClicked: { toast.controller.copyToClipboard(toast.artifact); toast.close(); } }
            Item { Layout.fillWidth: true }
        }
    }
}
