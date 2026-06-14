pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VaultiumUI

Item {
    id: screen
    property var controller
    signal restoreRequested(string archive)

    property var entries: []
    property string pendingDelete: ""
    property string pendingDeleteName: ""
    property string renameTarget: ""
    property string exportTarget: ""

    function humanSize(bytes) {
        if (bytes < 1024) return bytes + " B";
        var units = ["KB", "MB", "GB", "TB"];
        var value = bytes / 1024;
        var i = 0;
        while (value >= 1024 && i < units.length - 1) { value /= 1024; i++; }
        return value.toFixed(value < 10 ? 1 : 0) + " " + units[i];
    }

    function refresh() {
        if (screen.controller && dirField.text.length > 0) {
            screen.entries = screen.controller.listHistory(dirField.text);
        } else {
            screen.entries = [];
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space5

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            Text { text: qsTr("Backup History"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
            Text { text: qsTr("Artifacts found in a backup directory, with integrity status"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
        }

        // Directory selector
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            AppTextField {
                id: dirField
                Layout.fillWidth: true
                placeholderText: qsTr("/var/backups/vaultium")
                onAccepted: screen.refresh()
            }
            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: dirDialog.open() }
            AppButton { text: qsTr("Refresh"); iconName: "history"; variant: "primary"; onClicked: screen.refresh() }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: Theme.space2

            // Column header
            RowLayout {
                id: header
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.space3
                spacing: Theme.space4
                Text { text: qsTr("Artifact"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { text: qsTr("Source"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; font.weight: Font.DemiBold; Layout.preferredWidth: 120 }
                Text { text: qsTr("Size"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; font.weight: Font.DemiBold; Layout.preferredWidth: 70 }
                Text { text: qsTr("Integrity"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; font.weight: Font.DemiBold; Layout.preferredWidth: 90 }
                Item { Layout.preferredWidth: 100 }
            }
            Rectangle { anchors.top: header.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.topMargin: Theme.space2; height: 1; color: Theme.border }

            ListView {
                anchors.fill: parent
                anchors.topMargin: header.height + Theme.space4
                model: screen.entries
                clip: true
                spacing: 2

                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: 60
                    radius: Theme.radiusSm
                    color: rowHover.hovered ? Theme.muted : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space3
                        anchors.rightMargin: Theme.space3
                        spacing: Theme.space4

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Text { text: modelData.modified + (modelData.detail ? "  •  " + modelData.detail : ""); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; elide: Text.ElideRight; Layout.fillWidth: true }
                        }

                        StatusPill { Layout.preferredWidth: 120; tone: "info"; text: modelData.source }
                        Text { Layout.preferredWidth: 70; text: screen.humanSize(modelData.size); color: Theme.fgMuted; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs }
                        StatusPill {
                            Layout.preferredWidth: 90
                            tone: modelData.verified ? "success" : "neutral"
                            text: modelData.verified ? qsTr("Checksum") : qsTr("None")
                        }
                        IconButton { iconName: "play"; tip: qsTr("Open"); onClicked: screen.controller.openPath(modelData.path) }
                        IconButton { iconName: "folder"; tip: qsTr("Reveal in folder"); onClicked: screen.controller.revealInFolder(modelData.path) }
                        IconButton { iconName: "copy"; tip: qsTr("Copy path"); onClicked: screen.controller.copyToClipboard(modelData.path) }
                        IconButton { iconName: "edit"; tip: qsTr("Rename"); onClicked: { screen.renameTarget = modelData.path; renameField.text = modelData.name; renamePopup.open(); } }
                        IconButton { iconName: "export"; tip: qsTr("Export metadata"); onClicked: { screen.exportTarget = modelData.path; exportDialog.selectedFile = "file://" + modelData.path + ".meta.json"; exportDialog.open(); } }
                        IconButton { iconName: "trash"; tip: qsTr("Delete"); onClicked: { screen.pendingDelete = modelData.path; screen.pendingDeleteName = modelData.name; confirmDelete.open(); } }
                        AppButton {
                            Layout.preferredWidth: 100
                            text: qsTr("Restore")
                            iconName: "restore"
                            variant: "secondary"
                            onClicked: screen.restoreRequested(modelData.path)
                        }
                    }
                }
            }

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                visible: !screen.entries || screen.entries.length === 0
                spacing: Theme.space2
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 56; implicitHeight: 56; radius: Theme.radius; color: Theme.muted
                    AppIcon { anchors.centerIn: parent; name: "history"; color: Theme.fgSubtle; size: 26 }
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No artifacts found"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Pick a backup directory and refresh."); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
            }
        }
    }

    FolderDialog {
        id: dirDialog
        title: qsTr("Choose backup directory")
        onAccepted: { dirField.text = screen.controller.toLocalPath(selectedFolder.toString()); screen.refresh(); }
    }

    ConfirmDialog {
        id: confirmDelete
        danger: true
        title: qsTr("Delete backup?")
        message: qsTr("Permanently delete “%1” and its checksum/metadata sidecars? This cannot be undone.").arg(screen.pendingDeleteName)
        confirmText: qsTr("Delete")
        onConfirmed: { screen.controller.deleteArtifact(screen.pendingDelete); screen.refresh(); }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export metadata")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: screen.controller.exportMetadata(screen.exportTarget, screen.controller.toLocalPath(selectedFile.toString()))
    }

    Popup {
        id: renamePopup
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: 420
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { radius: Theme.radiusLg; color: Theme.surfaceAlt; border.color: Theme.borderStrong; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: Theme.space4
            Item { implicitHeight: Theme.space2 }
            Text {
                Layout.leftMargin: Theme.space5; Layout.rightMargin: Theme.space5
                text: qsTr("Rename backup"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.Bold
            }
            AppTextField {
                id: renameField
                Layout.fillWidth: true; Layout.leftMargin: Theme.space5; Layout.rightMargin: Theme.space5
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: Theme.space5; Layout.rightMargin: Theme.space5; Layout.bottomMargin: Theme.space5
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: renamePopup.close() }
                AppButton {
                    text: qsTr("Rename"); iconName: "check"; variant: "primary"
                    enabled: renameField.text.trim().length > 0
                    onClicked: { screen.controller.renameArtifact(screen.renameTarget, renameField.text); renamePopup.close(); screen.refresh(); }
                }
            }
        }
    }
}
