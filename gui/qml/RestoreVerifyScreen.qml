import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VaultiumUI

Item {
    id: screen
    property var controller
    property string initialArchive: ""

    onInitialArchiveChanged: if (initialArchive.length) archiveField.text = initialArchive

    readonly property bool ready: configField.text.length > 0 && archiveField.text.length > 0
    property string busyAction: "" // "verify" | "restore"

    function doRestore() {
        screen.busyAction = "restore";
        screen.controller.restoreArtifact(configField.text, archiveField.text, destField.text, overwriteSwitch.checked);
    }

    Connections {
        target: screen.controller
        function onBusyChanged() {
            if (!screen.controller.busy) {
                screen.busyAction = "";
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: screen.width
            spacing: Theme.space5

            Item { implicitHeight: Theme.space2; Layout.fillWidth: true }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                spacing: Theme.space1
                Text { text: qsTr("Restore / Verify"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Check an artifact's integrity, or restore it"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space4

                    // Config
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Config file"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true; spacing: Theme.space2
                            AppTextField { id: configField; Layout.fillWidth: true; placeholderText: qsTr("/etc/vaultium/vaultium.conf") }
                            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: configDialog.open() }
                        }
                    }

                    // Archive
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Backup archive"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true; spacing: Theme.space2
                            AppTextField { id: archiveField; Layout.fillWidth: true; placeholderText: qsTr("/var/backups/vaultium/…") }
                            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: archiveDialog.open() }
                        }
                    }

                    // Destination
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Restore destination"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true; spacing: Theme.space2
                            AppTextField { id: destField; Layout.fillWidth: true; placeholderText: qsTr("/tmp/restore") }
                            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: destDialog.open() }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                    // Overwrite + warning
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        AppSwitch { id: overwriteSwitch; text: qsTr("Apply changes (overwrite)") }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        visible: overwriteSwitch.checked
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        AppIcon { name: "alert"; color: Theme.warning; size: 16 }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Destructive: this writes over existing data. Without this switch the restore is a safe dry run.")
                            color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                spacing: Theme.space3
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Verify")
                    iconName: "shield-check"
                    variant: "secondary"
                    loading: screen.busyAction === "verify"
                    enabled: screen.ready && !screen.controller.busy
                    onClicked: {
                        screen.busyAction = "verify";
                        screen.controller.verifyArtifact(configField.text, archiveField.text);
                    }
                }
                AppButton {
                    text: overwriteSwitch.checked ? qsTr("Restore (apply)") : qsTr("Restore (dry run)")
                    iconName: overwriteSwitch.checked ? "alert" : "play"
                    variant: overwriteSwitch.checked ? "danger" : "primary"
                    loading: screen.busyAction === "restore"
                    enabled: screen.ready && !screen.controller.busy
                    onClicked: {
                        if (overwriteSwitch.checked) {
                            confirmDialog.open();
                        } else {
                            screen.doRestore();
                        }
                    }
                }
            }

            Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
        }
    }

    // Confirmation modal for destructive restore.
    Popup {
        id: confirmDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: 440
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surfaceAlt
            border.color: Theme.borderStrong
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.space4
            // padding handled via margins
            Item { implicitHeight: Theme.space2 }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space5
                Layout.rightMargin: Theme.space5
                spacing: Theme.space3
                Rectangle {
                    implicitWidth: 40; implicitHeight: 40; radius: Theme.radiusSm; color: Theme.dangerSoft
                    AppIcon { anchors.centerIn: parent; name: "alert"; color: Theme.danger; size: 22 }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: qsTr("Apply destructive restore?"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.Bold }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("This overwrites existing data at the destination and cannot be undone."); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space5
                Layout.rightMargin: Theme.space5
                Layout.bottomMargin: Theme.space5
                spacing: Theme.space2
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: confirmDialog.close() }
                AppButton { text: qsTr("Yes, overwrite"); variant: "danger"; iconName: "check"; onClicked: { confirmDialog.close(); screen.doRestore(); } }
            }
        }
    }

    FileDialog {
        id: configDialog
        title: qsTr("Select config file")
        onAccepted: configField.text = screen.controller.toLocalPath(selectedFile.toString())
    }
    FileDialog {
        id: archiveDialog
        title: qsTr("Select backup archive")
        onAccepted: archiveField.text = screen.controller.toLocalPath(selectedFile.toString())
    }
    FolderDialog {
        id: destDialog
        title: qsTr("Select restore destination")
        onAccepted: destField.text = screen.controller.toLocalPath(selectedFolder.toString())
    }
}
