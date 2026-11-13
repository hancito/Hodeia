import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl

TabButton {
    id: control
    padding: 8
    leftPadding: 14
    rightPadding: 14
    spacing: 6
    icon.width: 20
    icon.height: 20
    font.bold: checked

    readonly property color foreground: !enabled ? Colors.overlay0
                                      : checked ? Colors.primary
                                      : hovered ? Colors.text : Colors.overlay2
    icon.color: foreground

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.foreground
    }

    background: Item {
        implicitHeight: Style.controlHeight + 4

        // Hover / press feedback above the underline
        Rectangle {
            anchors.fill: parent
            anchors.bottomMargin: 3
            radius: Style.radiusSmall
            color: control.down ? Style.tint(Colors.text, 0.16)
                 : control.hovered && !control.checked ? Style.tint(Colors.text, 0.08)
                 : "transparent"
        }

        // Current-tab underline
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 8
            height: 2
            radius: 1
            color: Colors.primary
            visible: control.checked
        }
    }
}
