import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

Item {
    id: screen
    property var controller
    property var jobsModel
    signal createBackup()
    signal goRestore()
    signal goActivity()

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
                Text { text: qsTr("Dashboard"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Overview of your backup platform"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                columns: width < 720 ? 1 : 3
                columnSpacing: Theme.space4
                rowSpacing: Theme.space4

                StatCard { Layout.fillWidth: true; statIcon: "jobs"; statValue: screen.jobsModel ? screen.jobsModel.count : 0; statLabel: qsTr("Backup plans"); statColor: Theme.info }
                StatCard { Layout.fillWidth: true; statIcon: "play"; statValue: screen.controller ? screen.controller.status : qsTr("Idle"); statLabel: qsTr("Engine status"); statColor: Theme.accent }
                StatCard { Layout.fillWidth: true; statIcon: "database"; statValue: "3"; statLabel: qsTr("Source types available"); statColor: Theme.warning }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space4
                    Text { text: qsTr("Quick actions"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                    RowLayout {
                        spacing: Theme.space3
                        AppButton { text: qsTr("Create backup"); iconName: "create"; variant: "primary"; onClicked: screen.createBackup() }
                        AppButton { text: qsTr("Restore / Verify"); iconName: "restore"; variant: "secondary"; onClicked: screen.goRestore() }
                        AppButton { text: qsTr("View activity"); iconName: "history"; variant: "ghost"; onClicked: screen.goActivity() }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space3
                    Text { text: qsTr("Backup sources"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }

                    component SourceRow: RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space3
                        property string srcIcon: ""
                        property string srcName: ""
                        property string srcDesc: ""
                        AppIcon { name: srcIcon; color: Theme.fgMuted; size: 18 }
                        Text { text: srcName; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.Medium }
                        Text { text: srcDesc; color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; Layout.fillWidth: true; elide: Text.ElideRight }
                        StatusPill { tone: "success"; text: qsTr("Ready") }
                    }

                    SourceRow { srcIcon: "database"; srcName: qsTr("Database"); srcDesc: qsTr("MySQL / MariaDB, PostgreSQL, SQLite") }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    SourceRow { srcIcon: "folder"; srcName: qsTr("Filesystem"); srcDesc: qsTr("Arbitrary files and directories") }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                    SourceRow { srcIcon: "server"; srcName: qsTr("Service config"); srcDesc: qsTr("nginx, apache, systemd, docker, mysql, postgresql") }
                }
            }

            Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
        }
    }
}
