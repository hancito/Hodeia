import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: rightPane
    border.color: "black"
    border.width: 5
    radius: 10
    opacity: 0.5
    Text {
        anchors.centerIn: parent
        text: "View right"
    }
}
