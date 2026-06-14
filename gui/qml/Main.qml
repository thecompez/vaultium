pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

ApplicationWindow {
    id: window
    width: 1200
    height: 780
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    title: qsTr("Vaultium")
    color: Theme.bg
    Behavior on color { ColorAnimation { duration: Theme.durBase } }

    // Shared view-model layer (backed by vaultium_core).
    BackupController { id: controller }
    BackupJobsModel { id: jobsModel }
    AppState { id: appState }
    ScheduleViewModel { id: scheduleVm }
    ServersViewModel { id: serversVm }

    // The theme follows the user's preference, resolving "system" to the live OS scheme.
    Binding { target: Theme; property: "dark"; value: appState.effectiveDark }

    // Stack indices: 0 Dashboard, 1 Servers, 2 Backups, 3 Restore, 4 Schedules,
    // 5 Activity, 6 Settings, 7 Backup Wizard, 8 Manual create (launched flows).
    property int currentIndex: 0
    readonly property int wizardIndex: 7
    readonly property int manualIndex: 8
    property string pendingRestoreArchive: ""

    function gotoRestore(archivePath) {
        window.pendingRestoreArchive = archivePath;
        window.currentIndex = 3;
    }
    property string pendingServerId: ""
    function startBackup() { window.pendingServerId = ""; backupWizard.applyServer(""); window.currentIndex = window.wizardIndex; }
    function startBackupWithServer(id) { window.pendingServerId = id; backupWizard.applyServer(id); window.currentIndex = window.wizardIndex; }

    readonly property var navModel: [
        { label: qsTr("Dashboard"), icon: "dashboard" },
        { label: qsTr("Servers"),   icon: "server" },
        { label: qsTr("Backups"),   icon: "jobs" },
        { label: qsTr("Restore"),   icon: "restore" },
        { label: qsTr("Schedules"), icon: "clock" },
        { label: qsTr("Activity"),  icon: "history" },
        { label: qsTr("Settings"),  icon: "settings" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar --------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 252
            Layout.fillHeight: true
            color: Theme.surfaceAlt

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space4
                spacing: Theme.space5

                // Brand
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3
                    Rectangle {
                        implicitWidth: 38; implicitHeight: 38
                        radius: Theme.radiusSm
                        color: Theme.accentSoft
                        AppIcon { anchors.centerIn: parent; name: "shield"; color: Theme.accent; size: 22 }
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: "Vaultium"; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.Bold }
                        Text { text: qsTr("Backup platform"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        iconName: Theme.dark ? "sun" : "moon"
                        tip: Theme.dark ? qsTr("Switch to light mode") : qsTr("Switch to dark mode")
                        onClicked: appState.themeMode = appState.effectiveDark ? "light" : "dark"
                    }
                }

                // Navigation
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space1
                    Repeater {
                        model: window.navModel
                        delegate: NavItem {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.label
                            iconName: modelData.icon
                            active: window.currentIndex === index
                            onClicked: window.currentIndex = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Engine status footer
                Card {
                    Layout.fillWidth: true
                    padding: Theme.space4
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.space3
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text { text: qsTr("Engine"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                            Text { text: controller.status; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        StatusPill {
                            tone: controller.busy ? "warning" : "success"
                            text: controller.busy ? qsTr("Working") : qsTr("Ready")
                        }
                    }
                }
            }
        }

        // ---- Content --------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.currentIndex

                // 0 Dashboard
                DashboardScreen {
                    controller: controller
                    jobsModel: jobsModel
                    onCreateBackup: window.startBackup()
                    onGoRestore: window.currentIndex = 3
                    onGoActivity: window.currentIndex = 5
                }
                // 1 Servers
                ServersScreen {
                    serversVm: serversVm
                    onQuickBackup: (id) => window.startBackupWithServer(id)
                }
                // 2 Backups
                BackupJobsScreen {
                    jobsModel: jobsModel
                    controller: controller
                    onCreateBackup: window.startBackup()
                    onRunJob: (cfg) => controller.run(cfg)
                }
                // 3 Restore
                RestoreVerifyScreen {
                    controller: controller
                    initialArchive: window.pendingRestoreArchive
                }
                // 4 Schedules
                SchedulesScreen {
                    scheduleVm: scheduleVm
                    jobsModel: jobsModel
                    onCreateBackup: window.startBackup()
                }
                // 5 Activity
                BackupHistoryScreen {
                    controller: controller
                    onRestoreRequested: (archive) => window.gotoRestore(archive)
                }
                // 6 Settings
                SettingsScreen {
                    appState: appState
                }
                // 7 Backup Wizard (guided, discovery-driven)
                BackupWizard {
                    id: backupWizard
                    controller: controller
                    jobsModel: jobsModel
                    appState: appState
                    serversVm: serversVm
                    onDone: window.currentIndex = 2
                    onManualMode: window.currentIndex = window.manualIndex
                }
                // 8 Manual create (advanced)
                CreateBackupJobScreen {
                    controller: controller
                    jobsModel: jobsModel
                    appState: appState
                    onDone: window.currentIndex = 2
                }
            }

            TransferBar {
                Layout.fillWidth: true
                controller: controller
            }

            LogConsole {
                Layout.fillWidth: true
                controller: controller
            }
        }
    }

    // ---- Toast notifications ------------------------------------------------
    Toast { id: toast; controller: controller }

    Connections {
        target: controller
        function onOperationFinished(success, message) {
            toast.notify(success, message);
        }
    }
    Connections {
        target: scheduleVm
        function onOperationFinished(success, message) {
            toast.notify(success, message);
        }
    }
    Connections {
        target: serversVm
        function onOperationFinished(success, message) {
            toast.notify(success, message);
        }
    }
}
