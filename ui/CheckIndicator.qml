// Rounded check box square, shared by StyledCheckBox and MenuCheckRow.
// `partial`: a parent whose children are only partly checked (shows "–").
import QtQuick

Rectangle {
    id: box
    property bool checked: false
    property bool hovered: false
    property bool partial: false
    readonly property bool filled: checked || partial

    implicitWidth: Style.indicatorSize
    implicitHeight: Style.indicatorSize
    radius: 4
    color: filled ? Colors.primary : "transparent"
    border.width: 1.5
    border.color: filled ? Colors.primary : hovered ? Colors.overlay2 : Colors.overlay1

    Text {
        anchors.centerIn: parent
        visible: box.filled
        text: box.checked ? "✓" : "–"
        color: Colors.onPrimary
        font.bold: true
        font.pixelSize: box.height - 4
    }
}
