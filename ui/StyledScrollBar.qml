// Thin rounded scroll bar.  In a ScrollView, position it explicitly:
//   ScrollBar.vertical: StyledScrollBar {
//       parent: view; x: view.width - width
//       y: view.topPadding; height: view.availableHeight
//   }
import QtQuick
import QtQuick.Controls.Basic

ScrollBar {
    id: control
    padding: 2
    policy: ScrollBar.AsNeeded

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        // Nothing to scroll → no handle (AsNeeded); a full-length handle
        // would just sit on top of the content.
        visible: control.policy === ScrollBar.AlwaysOn || control.size < 1.0
        color: control.pressed ? Colors.overlay2
             : control.active ? Colors.overlay1 : Colors.surface2
    }

    background: Item {}
}
