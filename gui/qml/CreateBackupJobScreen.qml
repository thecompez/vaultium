import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VaultiumUI

Item {
    id: screen
    property var controller
    property var jobsModel
    property var appState
    readonly property bool advanced: appState ? appState.advancedMode : false
    signal done()

    readonly property string source: sourceCombo.currentText
    readonly property bool remote: execCombo.currentIndex === 1
    property string busyAction: "" // "test" | "run" — which button is loading
    readonly property bool canSave: nameField.text.length > 0
        && configPathField.text.length > 0
        && backupDirField.text.length > 0
        && (!remote || (hostField.text.length > 0 && userField.text.length > 0))

    function buildFields() {
        var f = {};
        f["BACKUP_SOURCE"] = screen.source;
        f["EXECUTION_MODE"] = screen.remote ? "remote_ssh" : "local";
        f["BACKUP_DIR"] = backupDirField.text;

        if (screen.remote) {
            f["REMOTE_HOST"] = hostField.text;
            f["REMOTE_PORT"] = portField.text.length ? portField.text : "22";
            f["REMOTE_USER"] = userField.text;
            f["REMOTE_AUTH_METHOD"] = authCombo.currentText;
            if (authCombo.currentText === "key") {
                f["REMOTE_IDENTITY_FILE"] = identityField.text;
                f["REMOTE_IDENTITY_PASSPHRASE"] = passphraseField.text;
            } else {
                f["REMOTE_PASSWORD"] = remotePassField.text;
            }
            f["REMOTE_SERVER_BACKUP_DIR"] = remoteServerDirField.text.length ? remoteServerDirField.text : "/tmp/vaultium_remote_backups";
            f["REMOTE_DOWNLOAD_DIR"] = backupDirField.text;
            f["REMOTE_REMOVE_AFTER_DOWNLOAD"] = removeAfterSwitch.checked;
            f["REMOTE_PROVISION_ENABLED"] = provisionSwitch.checked;
            f["REMOTE_CONNECT_TIMEOUT_SECONDS"] = connectTimeoutField.text.length ? connectTimeoutField.text : "15";
            f["REMOTE_COMMAND_TIMEOUT_SECONDS"] = cmdTimeoutField.text.length ? cmdTimeoutField.text : "600";
        }
        f["BACKUP_COMPRESS"] = compressSwitch.checked;
        f["BACKUP_CHECKSUM"] = checksumSwitch.checked;
        f["BACKUP_CLEANUP_ENABLED"] = true;
        f["BACKUP_RETENTION_DAYS"] = retentionField.text.length ? retentionField.text : "7";
        f["BACKUP_INTERVAL_MINUTES"] = "1440";
        f["LOCK_FILE"] = "/tmp/vaultium.lock";
        f["TAR_PATH"] = "/usr/bin/tar";
        f["GZIP_PATH"] = "/usr/bin/gzip";
        if (screen.source === "filesystem") {
            f["BACKUP_PATHS"] = pathsField.text;
        } else if (screen.source === "service-config") {
            f["BACKUP_SERVICES"] = servicesField.text;
        } else if (screen.source === "database") {
            f["BACKUP_ENGINE"] = engineCombo.currentText;
            f["BACKUP_DATABASES"] = databasesField.text.length ? databasesField.text : "all";
            if (engineCombo.currentText === "mysql") {
                f["MYSQLDUMP_PATH"] = "/usr/bin/mysqldump";
                f["MYSQL_DEFAULTS_FILE"] = dbExtraField.text;
            } else if (engineCombo.currentText === "postgresql") {
                f["PG_DUMP_PATH"] = "/usr/bin/pg_dump";
                f["PG_DUMPALL_PATH"] = "/usr/bin/pg_dumpall";
                f["POSTGRES_PASSWORD_FILE"] = dbExtraField.text;
            } else {
                f["SQLITE_FILES"] = dbExtraField.text;
            }
        }
        return f;
    }

    function save() {
        return screen.controller.writeConfig(buildFields(), configPathField.text);
    }

    // Clear the per-button loading flag when the controller finishes.
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
                Text { text: qsTr("Create Backup Job"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Define a job; Vaultium generates the config file for you"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: Theme.space5
                    rowSpacing: Theme.space4

                    // Name
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Job name"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppTextField { id: nameField; Layout.fillWidth: true; placeholderText: qsTr("My website backup") }
                    }
                    // Source
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Source type"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppComboBox { id: sourceCombo; Layout.fillWidth: true; model: ["filesystem", "database", "service-config"] }
                    }

                    // Execution mode
                    ColumnLayout {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Where does the backup run?"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppComboBox { id: execCombo; Layout.fillWidth: true; model: [qsTr("Local machine"), qsTr("Remote server (SSH) → download to this client")] }
                    }

                    // --- Remote SSH section ---
                    ColumnLayout {
                        visible: screen.remote
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space3

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            AppIcon { name: "server"; color: Theme.info; size: 18 }
                            Text { text: qsTr("Remote server"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("Test connection")
                                iconName: "shield-check"
                                variant: "secondary"
                                loading: screen.busyAction === "test"
                                enabled: hostField.text.length > 0 && userField.text.length > 0 && !screen.controller.busy
                                onClicked: {
                                    var p = screen.controller.writeTempConfig(screen.buildFields());
                                    if (p.length) {
                                        screen.busyAction = "test";
                                        screen.controller.testRemoteConnection(p);
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.space5
                            rowSpacing: Theme.space3

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Host"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: hostField; Layout.fillWidth: true; placeholderText: qsTr("server.example.com") }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Port"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: portField; Layout.fillWidth: true; text: "22" }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("User"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: userField; Layout.fillWidth: true; placeholderText: qsTr("ubuntu") }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Authentication"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppComboBox { id: authCombo; Layout.fillWidth: true; model: ["key", "password"] }
                            }
                            ColumnLayout {
                                visible: authCombo.currentText === "key"
                                Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Private key file"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: Theme.space2
                                    AppTextField { id: identityField; Layout.fillWidth: true; placeholderText: qsTr("~/.ssh/id_ed25519") }
                                    AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: identityDialog.open() }
                                }
                            }
                            ColumnLayout {
                                visible: authCombo.currentText === "key"
                                Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Key passphrase"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: passphraseField; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("leave empty if the key is not encrypted") }
                            }
                            ColumnLayout {
                                visible: authCombo.currentText === "password"
                                Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Password"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: remotePassField; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("••••••••") }
                            }
                            ColumnLayout {
                                Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Remote working directory (on the server)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: remoteServerDirField; Layout.fillWidth: true; placeholderText: qsTr("/home/ubuntu/vaultium_remote_backups") }
                            }
                            ColumnLayout {
                                visible: screen.advanced
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Connect timeout (s)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: connectTimeoutField; Layout.fillWidth: true; text: "15" }
                            }
                            ColumnLayout {
                                visible: screen.advanced
                                Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Command timeout (s)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: cmdTimeoutField; Layout.fillWidth: true; text: "600" }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space5
                            AppSwitch { id: removeAfterSwitch; text: qsTr("Delete remote copy after download"); checked: true }
                            AppSwitch { id: provisionSwitch; visible: screen.advanced; text: qsTr("Provision server (create dirs / configs)") }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    }

                    // --- Filesystem fields ---
                    ColumnLayout {
                        visible: screen.source === "filesystem"
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Paths to back up"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            AppTextField { id: pathsField; Layout.fillWidth: true; placeholderText: qsTr("/srv/www, /etc/myapp") }
                            AppButton { text: qsTr("Add folder"); iconName: "folder"; variant: "secondary"; onClicked: pathFolderDialog.open() }
                        }
                        Text { text: qsTr("Comma-separated absolute paths."); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    }

                    // --- Service-config fields ---
                    ColumnLayout {
                        visible: screen.source === "service-config"
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Services"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppTextField { id: servicesField; Layout.fillWidth: true; placeholderText: qsTr("nginx, systemd, docker") }
                        Text { text: qsTr("Known: nginx, apache, systemd, docker, mysql, postgresql. Missing paths are skipped."); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    }

                    // --- Database fields ---
                    ColumnLayout {
                        visible: screen.source === "database"
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Engine"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppComboBox { id: engineCombo; Layout.fillWidth: true; model: ["mysql", "postgresql", "sqlite"] }
                    }
                    ColumnLayout {
                        visible: screen.source === "database"
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Databases"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppTextField { id: databasesField; Layout.fillWidth: true; placeholderText: qsTr("all  (or app_db, analytics)") }
                    }
                    ColumnLayout {
                        visible: screen.source === "database"
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text {
                            text: engineCombo.currentText === "mysql" ? qsTr("MySQL defaults file")
                                : engineCombo.currentText === "postgresql" ? qsTr("Postgres password file")
                                : qsTr("SQLite files")
                            color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium
                        }
                        AppTextField { id: dbExtraField; Layout.fillWidth: true; placeholderText: qsTr("/etc/vaultium/mysql.cnf") }
                    }

                    // Backup destination
                    ColumnLayout {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: screen.remote ? qsTr("Download directory (on this client)") : qsTr("Backup destination"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            AppTextField { id: backupDirField; Layout.fillWidth: true; placeholderText: screen.remote ? qsTr("/Users/me/Backups") : qsTr("/var/backups/vaultium") }
                            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: backupDirDialog.open() }
                        }
                    }

                    // Options
                    RowLayout {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space5
                        AppSwitch { id: compressSwitch; text: qsTr("Compress (gzip)"); checked: true }
                        AppSwitch { id: checksumSwitch; text: qsTr("SHA-256 checksum"); checked: true }
                        ColumnLayout {
                            visible: screen.advanced
                            spacing: Theme.space2
                            Text { text: qsTr("Retention (days)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                            AppTextField { id: retentionField; implicitWidth: 90; text: "7" }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Config save path
                    ColumnLayout {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Save config to"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            AppTextField { id: configPathField; Layout.fillWidth: true; placeholderText: qsTr("/etc/vaultium/vaultium.conf") }
                            AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: configSaveDialog.open() }
                        }
                    }
                }
            }

            // Actions
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                spacing: Theme.space3
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Save & add job")
                    iconName: "check"
                    variant: "secondary"
                    enabled: screen.canSave
                    onClicked: {
                        if (screen.save()) {
                            screen.jobsModel.addJob(nameField.text, screen.source, screen.controller.toLocalPath(configPathField.text));
                            screen.done();
                        }
                    }
                }
                AppButton {
                    text: screen.remote ? qsTr("Save & run remote backup") : qsTr("Save & run now")
                    iconName: screen.remote ? "download" : "play"
                    variant: "primary"
                    loading: screen.busyAction === "run"
                    enabled: screen.canSave && !screen.controller.busy
                    onClicked: {
                        if (screen.save()) {
                            screen.jobsModel.addJob(nameField.text, screen.source, screen.controller.toLocalPath(configPathField.text));
                            screen.busyAction = "run";
                            screen.controller.run(configPathField.text);
                        }
                    }
                }
            }

            Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
        }
    }

    FolderDialog {
        id: pathFolderDialog
        title: qsTr("Add a folder to back up")
        onAccepted: {
            var p = screen.controller.toLocalPath(selectedFolder.toString());
            pathsField.text = pathsField.text.length ? (pathsField.text + ", " + p) : p;
        }
    }
    FolderDialog {
        id: backupDirDialog
        title: qsTr("Choose backup destination")
        onAccepted: backupDirField.text = screen.controller.toLocalPath(selectedFolder.toString())
    }
    FileDialog {
        id: configSaveDialog
        title: qsTr("Save configuration file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "conf"
        nameFilters: [qsTr("Config files (*.conf)"), qsTr("All files (*)")]
        onAccepted: configPathField.text = screen.controller.toLocalPath(selectedFile.toString())
    }
    FileDialog {
        id: identityDialog
        title: qsTr("Select SSH private key")
        onAccepted: identityField.text = screen.controller.toLocalPath(selectedFile.toString())
    }
}
