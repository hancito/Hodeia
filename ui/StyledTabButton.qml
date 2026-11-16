import QtQuick
import QtQuick.Controls.Basic

TabButton {
    id: control
    padding: 8
    leftPadding: 14
    rightPadding: 14
    spacing: 6
    font.bold: checked

    readonly property color foreground: !enabled ? Colors.overlay0
                                      : checked ? Colors.primary
                                      : hovered ? Colors.text : Colors.overlay2

    // Text rather than IconLabel: IconLabel cannot elide, so labels spilled
    // over neighbouring tabs when the bar got narrow.  (No tab uses an icon.)
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.foreground
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
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
