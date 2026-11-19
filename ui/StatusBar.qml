// Window footer: flat bar with a single status line.  Set `text` directly
// (it is also what code holding a "statusBarLabel" reference writes to).
// `detailText` shows ongoing background work at the right; `actionText`
// adds a clickable link at the right end (actionClicked).
import QtQuick
import QtQuick.Controls.Basic

ToolBar {
    id: bar
    property alias text: label.text
    property string actionText: ""
    // Ongoing background work, shown at the right (before the link).
    property string detailText: ""
    signal actionClicked

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
        anchors.left: parent.left
        anchors.right: detail.visible ? detail.left : action.visible ? action.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        leftPadding: Style.paneMargin
        rightPadding: Style.paneMargin
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: Fonts.mono
        font.pixelSize: Style.fontSmall
        color: Colors.overlay2
    }

    Label {
        id: detail
        visible: bar.detailText !== ""
        anchors.right: action.visible ? action.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        leftPadding: Style.paneMargin
        rightPadding: Style.paneMargin
        verticalAlignment: Text.AlignVCenter
        text: bar.detailText
        font.family: Fonts.mono
        font.pixelSize: Style.fontSmall
        color: Colors.overlay2
    }

    // Optional link at the right end (e.g. "3 new PDFs in Zotero").
    Label {
        id: action
        visible: bar.actionText !== ""
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        leftPadding: Style.paneMargin
        rightPadding: Style.paneMargin
        verticalAlignment: Text.AlignVCenter
        text: bar.actionText
        font.family: Fonts.mono
        font.pixelSize: Style.fontSmall
        font.underline: actionArea.containsMouse
        color: Colors.primary

        MouseArea {
            id: actionArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: bar.actionClicked()
        }
    }
}
