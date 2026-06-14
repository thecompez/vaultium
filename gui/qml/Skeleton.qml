import QtQuick
import VaultiumUI

// A shimmering placeholder block for loading states.
Rectangle {
    id: root
    radius: Theme.radiusSm
    color: Theme.muted
    clip: true

    Rectangle {
        id: shimmer
        width: parent.width * 0.5
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, Theme.dark ? 0.06 : 0.5) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        XAnimator on x {
            from: -root.width * 0.5
            to: root.width
            duration: 1100
            loops: Animation.Infinite
            running: root.visible
        }
    }
}
