import QtQuick
import QtQuick.Controls
import QtQuick.Layouts


Rectangle {
    id: centralPane
    border.color: "black"
    border.width: 5
    radius: 100
    color: "#f5f5f5"
    anchors.rightMargin: -radius
    opacity: 0.5

    Text {
                anchors.centerIn: parent
                text: "View central"
            }
}