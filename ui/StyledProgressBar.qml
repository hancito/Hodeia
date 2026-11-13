// Thin rounded progress bar (determinate only — `indeterminate` is not used
// in the app).
import QtQuick
import QtQuick.Controls.Basic

ProgressBar {
    id: control

    contentItem: Item {
        implicitWidth: 200
        implicitHeight: 6

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Colors.primary
            Behavior on width {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 6
        radius: 3
        color: Colors.surface1
    }
}
