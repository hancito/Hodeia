// SplitView handle: an invisible gap between panes that shows a small grip
// on hover and turns accent-coloured while dragging.
import QtQuick
import QtQuick.Controls

Item {
    id: handle
    implicitWidth: Style.paneMargin
    implicitHeight: Style.paneMargin

    Rectangle {
        anchors.centerIn: parent
        width: 3
        height: 48
        radius: 1.5
        color: handle.SplitHandle.pressed ? Colors.primary : Colors.overlay0
        opacity: handle.SplitHandle.hovered || handle.SplitHandle.pressed ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 120
            }
        }
    }
}
