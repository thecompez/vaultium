import QtQuick
import QtQuick.Controls.Basic
import VaultiumUI

// A surface panel: rounded, bordered, padded. Content auto-sizes via Pane.
Pane {
    id: card
    padding: Theme.space5

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
    }
}
