// Tab bar with underline tabs (use StyledTabButton children): a thin base
// line across the bar, the current tab underlined in the accent colour.
import QtQuick
import QtQuick.Controls.Basic

TabBar {
    id: control
    spacing: 4

    background: Item {
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Colors.surface1
        }
    }
}
