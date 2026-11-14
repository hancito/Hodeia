// Message window for user-facing information and errors (replaces
// console.log for anything the user should see).  Centred on its window;
// long messages scroll; errors get a warning sign in the title.  Usage:
//     MessageBox { id: messageBox }
//     messageBox.showInfo(qsTr("Title"), qsTr("Text"))
//     messageBox.showError(qsTr("Title"), qsTr("Text"))
import QtQuick
import QtQuick.Controls.Basic

StyledDialog {
    id: control
    property string message

    function showInfo(title, text) {
        show(title, text, false);
    }
    function showError(title, text) {
        show(title, text, true);
    }
    function show(title, text, isError) {
        control.title = isError ? "⚠ " + title : title;
        control.message = text;
        control.open();
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    standardButtons: Dialog.Ok

    ScrollView {
        anchors.fill: parent
        implicitHeight: Math.min(messageLabel.implicitHeight, 320)
        contentWidth: availableWidth
        Label {
            id: messageLabel
            width: control.availableWidth
            text: control.message
            wrapMode: Text.WordWrap
        }
    }
}
