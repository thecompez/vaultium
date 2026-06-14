import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

Item {
    id: screen
    property var appState

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
                Text { text: qsTr("Settings"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Application preferences and defaults"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space4
                    Text { text: qsTr("Experience level"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space3
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Mode"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                            Text {
                                text: (screen.appState && screen.appState.advancedMode)
                                    ? qsTr("Advanced — exposes paths, retention, SSH tuning and verification options")
                                    : qsTr("Simple — recommended choices, plain language, advanced settings hidden")
                                color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                                wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                        SegmentedControl {
                            options: [qsTr("Simple"), qsTr("Advanced")]
                            currentIndex: (screen.appState && screen.appState.advancedMode) ? 1 : 0
                            onActivated: (i) => { if (screen.appState) screen.appState.advancedMode = (i === 1); }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space4
                    Text { text: qsTr("Appearance"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space3
                        AppIcon { name: Theme.dark ? "moon" : "sun"; color: Theme.fgMuted; size: 18 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Theme"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                            Text {
                                text: (screen.appState && screen.appState.themeMode === "system")
                                    ? qsTr("Following your system setting (currently %1)").arg(screen.appState.systemDark ? qsTr("dark") : qsTr("light"))
                                    : qsTr("Always use the %1 theme").arg(Theme.dark ? qsTr("dark") : qsTr("light"))
                                color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                                wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                        SegmentedControl {
                            options: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                            currentIndex: screen.appState ? (screen.appState.themeMode === "light" ? 1 : screen.appState.themeMode === "dark" ? 2 : 0) : 0
                            onActivated: (i) => { if (screen.appState) screen.appState.themeMode = (i === 1 ? "light" : i === 2 ? "dark" : "system"); }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space4
                    Text { text: qsTr("Defaults"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Default backup directory"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppTextField { Layout.fillWidth: true; text: "/var/backups/vaultium" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Text { text: qsTr("Default config directory"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                        AppTextField { Layout.fillWidth: true; text: "/etc/vaultium" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppSwitch { text: qsTr("Verify checksum before every restore"); checked: true }
                    }
                    RowLayout {
                        spacing: Theme.space2
                        AppButton { text: qsTr("Save preferences"); iconName: "check"; variant: "primary" }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space6
                Layout.rightMargin: Theme.space6
                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.space4
                    Rectangle {
                        implicitWidth: 44; implicitHeight: 44; radius: Theme.radiusSm; color: Theme.accentSoft
                        AppIcon { anchors.centerIn: parent; name: "shield"; color: Theme.accent; size: 22 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Vaultium"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.Bold }
                        Text { text: qsTr("Cross-platform backup platform • version 0.2.0"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                    }
                    StatusPill { tone: "success"; text: qsTr("Core ready") }
                }
            }

            Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
        }
    }
}
