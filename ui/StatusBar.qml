// Window footer: flat bar with a single status line.  Set `text` directly
// (it is also what code holding a "statusBarLabel" reference writes to).
import QtQuick
import QtQuick.Controls.Basic

ToolBar {
    id: bar
    property alias text: label.text

    padding: 0
    implicitHeight: 28

    background: Rectangle {
        color: Colors.mantle
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Colors.surface0
        }
    }

    Label {
        id: label
        anchors.fill: parent
        leftPadding: Style.paneMargin
        rightPadding: Style.paneMargin
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: Fonts.mono
        font.pixelSize: Style.fontSmall
        color: Colors.overlay2
    }
}
