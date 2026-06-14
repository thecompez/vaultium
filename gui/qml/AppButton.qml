import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// Themed button with variants: primary | secondary | danger | ghost.
// Optional leading icon via `iconName`.
Button {
    id: control

    property string variant: "primary"
    property string iconName: ""
    property bool loading: false

    readonly property color _base: variant === "primary" ? Theme.accent
        : variant === "danger" ? Theme.danger
        : variant === "secondary" ? Theme.secondary
        : "transparent"
    readonly property color _hover: variant === "primary" ? Theme.accentHover
        : variant === "danger" ? Theme.dangerHover
        : variant === "secondary" ? Theme.secondaryHover
        : Theme.muted
    readonly property color _text: variant === "primary" ? Theme.textOnAccent
        : variant === "danger" ? Theme.textOnDanger
        : Theme.fg

    implicitHeight: 40
    leftPadding: Theme.space4
    rightPadding: Theme.space4
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsBase
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.45

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: Math.max(contentRow.implicitHeight, 18)

        // Loading spinner (themed to the button's text color).
        Item {
            id: spinner
            anchors.centerIn: parent
            width: 18; height: 18
            visible: control.loading
            Repeater {
                model: 8
                delegate: Rectangle {
                    required property int index
                    width: 3; height: 3; radius: 1.5
                    color: control._text
                    opacity: 0.15 + 0.85 * (index / 8)
                    x: 9 + 7 * Math.cos(index / 8 * 2 * Math.PI) - 1.5
                    y: 9 + 7 * Math.sin(index / 8 * 2 * Math.PI) - 1.5
                }
            }
            RotationAnimator on rotation {
                from: 0; to: 360; duration: 800
                loops: Animation.Infinite
                running: control.loading
            }
        }

        Row {
            id: contentRow
            anchors.centerIn: parent
            opacity: control.loading ? 0 : 1
            spacing: Theme.space2
            AppIcon {
                visible: control.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: 16
                color: control._text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control._text
                font: control.font
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.down ? Qt.darker(control._hover, 1.1)
            : control.hovered ? control._hover : control._base
        // Only the secondary (outlined) variant has a border; ghost is borderless.
        border.width: control.variant === "secondary" ? 1 : 0
        border.color: Theme.borderStrong

        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
