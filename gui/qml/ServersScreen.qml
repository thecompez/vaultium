pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VaultiumUI

Item {
    id: screen
    property var serversVm
    signal quickBackup(string serverId)   // "use this server in a backup"

    property string searchText: ""

    function statusTone(s) {
        if (s === "ok") return "success";
        if (s === "failed") return "danger";
        return "neutral";
    }
    function matches(s) {
        if (searchText.length === 0) return true;
        var q = searchText.toLowerCase();
        return (s.name + " " + s.host + " " + s.user + " " + s.tags).toLowerCase().indexOf(q) >= 0;
    }
    // Favorites first, then by name.
    property var visibleServers: {
        if (!serversVm) return [];
        var a = serversVm.servers.filter(screen.matches);
        a.sort(function (x, y) {
            if (x.favorite !== y.favorite) return x.favorite ? -1 : 1;
            return x.name.toLowerCase() < y.name.toLowerCase() ? -1 : 1;
        });
        return a;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space5

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space4
            ColumnLayout {
                spacing: Theme.space1
                Layout.alignment: Qt.AlignVCenter
                Text { text: qsTr("Servers"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Save a server once, reuse it for every backup, schedule and restore"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }
            Item { Layout.fillWidth: true }
            AppTextField {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 220
                visible: screen.serversVm && screen.serversVm.servers.length > 0
                placeholderText: qsTr("Search servers…")
                onTextChanged: screen.searchText = text
            }
            AppButton { Layout.alignment: Qt.AlignVCenter; text: qsTr("Add server"); iconName: "server"; variant: "primary"; onClicked: form.openNew() }
        }

        Banner {
            visible: screen.serversVm && !screen.serversVm.keychainAvailable
            Layout.fillWidth: true; tone: "warning"
            text: qsTr("No system keychain detected — passwords can't be stored securely. Prefer key-based authentication on this platform.")
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: Theme.space2

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: !screen.serversVm || screen.serversVm.servers.length === 0
                iconName: "server"
                title: qsTr("No servers connected yet")
                body: qsTr("Add a server to save its connection once. Backups, schedules and discovery all reuse it — no re-typing credentials.")
                actionText: qsTr("Add your first server")
                onActionClicked: form.openNew()
            }

            ListView {
                anchors.fill: parent
                visible: screen.visibleServers.length > 0
                model: screen.visibleServers
                clip: true
                spacing: Theme.space2

                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    implicitHeight: 78
                    radius: Theme.radiusSm
                    color: rowHover.hovered ? Theme.muted : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space3
                        anchors.rightMargin: Theme.space4
                        spacing: Theme.space3

                        IconButton {
                            iconName: modelData.favorite ? "check-circle" : "shield"
                            iconColor: modelData.favorite ? Theme.warning : Theme.fgSubtle
                            tip: modelData.favorite ? qsTr("Unpin") : qsTr("Pin to top")
                            onClicked: screen.serversVm.setFavorite(modelData.id, !modelData.favorite)
                        }
                        Rectangle {
                            implicitWidth: 40; implicitHeight: 40; radius: Theme.radiusSm; color: Theme.muted
                            AppIcon { anchors.centerIn: parent; name: "server"; size: 20; color: Theme.fgMuted }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: Theme.space2
                                Text { text: modelData.name.length ? modelData.name : qsTr("Untitled server"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.DemiBold }
                                StatusPill { tone: screen.statusTone(modelData.lastStatus); text: modelData.lastStatus === "ok" ? qsTr("reachable") : modelData.lastStatus === "failed" ? qsTr("unreachable") : qsTr("untested") }
                                StatusPill { visible: modelData.tags.length > 0; tone: "info"; text: modelData.tags }
                            }
                            Text {
                                text: modelData.user + "@" + modelData.host + ":" + modelData.port
                                    + "   •   " + qsTr("last ok: ") + modelData.lastConnected
                                color: Theme.fgSubtle; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }

                        AppButton {
                            text: qsTr("Test")
                            iconName: "shield-check"
                            variant: "secondary"
                            loading: screen.serversVm.testingId === modelData.id
                            enabled: screen.serversVm.testingId.length === 0
                            onClicked: screen.serversVm.testConnection(modelData.id)
                        }
                        IconButton { iconName: "play"; tip: qsTr("Back up this server"); onClicked: screen.quickBackup(modelData.id) }
                        IconButton { iconName: "settings"; tip: qsTr("Edit"); onClicked: form.openEdit(modelData) }
                        IconButton { iconName: "copy"; tip: qsTr("Duplicate"); onClicked: screen.serversVm.duplicateServer(modelData.id) }
                        IconButton { iconName: "trash"; tip: qsTr("Delete"); onClicked: { screen.pendingDelete = modelData.id; screen.pendingName = modelData.name; confirmDelete.open(); } }
                    }
                }
            }
        }
    }

    property string pendingDelete: ""
    property string pendingName: ""
    ConfirmDialog {
        id: confirmDelete
        danger: true
        title: qsTr("Delete server?")
        message: qsTr("Remove “%1” and its stored credentials from the keychain?").arg(screen.pendingName)
        confirmText: qsTr("Delete")
        onConfirmed: screen.serversVm.removeServer(screen.pendingDelete)
    }

    // ---- Add / edit form ----------------------------------------------------
    Popup {
        id: form
        anchors.centerIn: Overlay.overlay
        modal: true; focus: true; width: 540; padding: 0
        closePolicy: Popup.CloseOnEscape

        property string editId: ""
        property int authIndex: 0   // 0 password, 1 key

        function openNew() {
            editId = ""; nameField.text = ""; hostField.text = ""; portField.text = "22";
            userField.text = ""; authIndex = 0; keyField.text = ""; secretField.text = "";
            notesField.text = ""; tagsField.text = ""; favSwitch.checked = false;
            open();
        }
        function openEdit(s) {
            editId = s.id; nameField.text = s.name; hostField.text = s.host; portField.text = s.port;
            userField.text = s.user; authIndex = (s.authMethod === "key") ? 1 : 0;
            keyField.text = s.keyPath; secretField.text = ""; notesField.text = s.notes;
            tagsField.text = s.tags; favSwitch.checked = s.favorite;
            open();
        }
        function save() {
            var f = {
                "id": form.editId,
                "name": nameField.text,
                "host": hostField.text,
                "port": portField.text,
                "user": userField.text,
                "authMethod": form.authIndex === 1 ? "key" : "password",
                "keyPath": keyField.text,
                "notes": notesField.text,
                "tags": tagsField.text,
                "favorite": favSwitch.checked
            };
            if (secretField.text.length > 0) f["secret"] = secretField.text;
            screen.serversVm.saveServer(f);
            form.close();
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { radius: Theme.radiusLg; color: Theme.surfaceAlt; border.color: Theme.borderStrong; border.width: 1 }

        contentItem: ColumnLayout {
            spacing: Theme.space4
            Text {
                Layout.margins: Theme.space5; Layout.bottomMargin: 0
                text: form.editId ? qsTr("Edit server") : qsTr("Add server")
                color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLg; font.weight: Font.Bold
            }

            GridLayout {
                Layout.fillWidth: true; Layout.leftMargin: Theme.space5; Layout.rightMargin: Theme.space5
                columns: 2; columnSpacing: Theme.space4; rowSpacing: Theme.space3

                ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Name"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: nameField; Layout.fillWidth: true; placeholderText: qsTr("Production Server") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Host / IP"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: hostField; Layout.fillWidth: true; placeholderText: qsTr("server.example.com") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Port"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: portField; Layout.fillWidth: true; text: "22" }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("User"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: userField; Layout.fillWidth: true; placeholderText: qsTr("root") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Authentication"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppComboBox { Layout.fillWidth: true; model: ["password", "key"]; currentIndex: form.authIndex; onActivated: (i) => form.authIndex = i }
                }
                ColumnLayout { visible: form.authIndex === 1; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Private key file"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    RowLayout { Layout.fillWidth: true; spacing: Theme.space2
                        AppTextField { id: keyField; Layout.fillWidth: true; placeholderText: qsTr("~/.ssh/id_ed25519") }
                        AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: keyDialog.open() }
                    }
                }
                ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: form.authIndex === 1 ? qsTr("Key passphrase (stored in keychain)") : qsTr("Password (stored in keychain)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: secretField; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: form.editId ? qsTr("leave blank to keep current") : "" }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Tags (optional)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: tagsField; Layout.fillWidth: true; placeholderText: qsTr("production") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2; Layout.alignment: Qt.AlignBottom
                    AppSwitch { id: favSwitch; text: qsTr("Pin to top") }
                }
                ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Notes (optional)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: notesField; Layout.fillWidth: true }
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.margins: Theme.space5; Layout.topMargin: 0
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: form.close() }
                AppButton { text: qsTr("Save server"); iconName: "check"; variant: "primary"; enabled: nameField.text.length > 0 && hostField.text.length > 0 && userField.text.length > 0; onClicked: form.save() }
            }
        }
    }

    FileDialog {
        id: keyDialog
        title: qsTr("Select SSH private key")
        onAccepted: keyField.text = (selectedFile + "").replace("file://", "")
    }
}
