import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

// Reusable confirmation modal. Set title/message/confirmText; `danger` styles the
// confirm button. Emits `confirmed()` when accepted.
Popup {
    id: root
    property string title: ""
    property string message: ""
    property string confirmText: qsTr("Confirm")
    property string cancelText: qsTr("Cancel")
    property string iconName: danger ? "alert" : "info"
    property bool danger: false
    signal confirmed()

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

        Item { implicitHeight: Theme.space2 }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space5
            Layout.rightMargin: Theme.space5
            spacing: Theme.space3

            Rectangle {
                implicitWidth: 40; implicitHeight: 40
                radius: Theme.radiusSm
                color: root.danger ? Theme.dangerSoft : Theme.infoSoft
                AppIcon {
                    anchors.centerIn: parent
                    name: root.iconName
                    color: root.danger ? Theme.danger : Theme.info
                    size: 22
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.title
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsMd
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.message !== ""
                    text: root.message
                    color: Theme.fgMuted
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fsSm
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space5
            Layout.rightMargin: Theme.space5
            Layout.bottomMargin: Theme.space5
            spacing: Theme.space2
            Item { Layout.fillWidth: true }
            AppButton { text: root.cancelText; variant: "ghost"; onClicked: root.close() }
            AppButton {
                text: root.confirmText
                variant: root.danger ? "danger" : "primary"
                iconName: "check"
                onClicked: { root.close(); root.confirmed(); }
            }
        }
    }
}
