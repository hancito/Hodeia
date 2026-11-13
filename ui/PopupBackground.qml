// Shared card look for menus, combo box popups and dialogs.
import QtQuick
import QtQuick.Effects

Rectangle {
    implicitWidth: 200
    implicitHeight: Style.rowHeight
    color: Colors.base
    border.color: Colors.surface1
    border.width: 1
    radius: Style.radiusLarge
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: 0.35
        shadowBlur: 0.6
        shadowVerticalOffset: 3
    }
}
