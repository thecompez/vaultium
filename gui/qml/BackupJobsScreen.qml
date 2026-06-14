pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

Item {
    id: screen
    property var jobsModel
    property var controller
    signal createBackup()
    signal runJob(string configPath)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space5

        // Header with primary action
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space4
            ColumnLayout {
                spacing: Theme.space1
                Layout.alignment: Qt.AlignVCenter
                Text { text: qsTr("Backup Jobs"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Saved backup configurations you can run or restore"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }
            Item { Layout.fillWidth: true }
            AppButton { Layout.alignment: Qt.AlignVCenter; text: qsTr("New backup"); iconName: "create"; variant: "primary"; onClicked: screen.createBackup() }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: Theme.space2

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                visible: !screen.jobsModel || screen.jobsModel.count === 0
                spacing: Theme.space3
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 56; implicitHeight: 56; radius: Theme.radius
                    color: Theme.muted
                    AppIcon { anchors.centerIn: parent; name: "jobs"; color: Theme.fgSubtle; size: 26 }
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No backup jobs yet"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Create a job to define what to back up and how."); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                AppButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("Create your first backup"); iconName: "create"; onClicked: screen.createBackup() }
            }

            ListView {
                id: list
                anchors.fill: parent
                visible: screen.jobsModel && screen.jobsModel.count > 0
                model: screen.jobsModel
                clip: true
                spacing: Theme.space2

                delegate: Rectangle {
                    required property int index
                    required property string name
                    required property string sourceType
                    required property string configPath

                    width: ListView.view.width
                    implicitHeight: 68
                    radius: Theme.radiusSm
                    color: hover.hovered ? Theme.muted : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                    HoverHandler { id: hover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space4
                        anchors.rightMargin: Theme.space4
                        spacing: Theme.space4

                        Rectangle {
                            implicitWidth: 40; implicitHeight: 40; radius: Theme.radiusSm
                            color: Theme.muted
                            AppIcon {
                                anchors.centerIn: parent
                                size: 20
                                color: Theme.fgMuted
                                name: sourceType === "filesystem" ? "folder"
                                    : sourceType === "service-config" ? "server" : "database"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.DemiBold }
                            Text { text: configPath; color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        }

                        StatusPill { tone: "info"; text: sourceType }

                        AppButton { text: qsTr("Run"); iconName: "play"; variant: "secondary"; onClicked: screen.runJob(configPath) }
                        IconButton {
                            iconName: "trash"
                            iconColor: Theme.fgMuted
                            tip: qsTr("Remove job")
                            onClicked: screen.jobsModel.removeJob(index)
                        }
                    }
                }
            }
        }
    }
}
