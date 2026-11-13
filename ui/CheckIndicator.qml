// Rounded check box square, shared by StyledCheckBox and MenuCheckRow.
import QtQuick

Rectangle {
    id: box
    property bool checked: false
    property bool hovered: false

    implicitWidth: Style.indicatorSize
    implicitHeight: Style.indicatorSize
    radius: 4
    color: checked ? Colors.primary : "transparent"
    border.width: 1.5
    border.color: checked ? Colors.primary : hovered ? Colors.overlay2 : Colors.overlay1

    Text {
        anchors.centerIn: parent
        visible: box.checked
        text: "✓"
        color: Colors.onPrimary
        font.bold: true
        font.pixelSize: box.height - 4
    }
}
