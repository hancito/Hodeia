// Message window for user-facing information and errors (replaces
// console.log for anything the user should see).  Centred on its window;
// long messages scroll; errors get a warning sign in the title.  A message
// that arrives while another is shown waits its turn.  Usage:
//     MessageBox { id: messageBox }
//     messageBox.showInfo(qsTr("Title"), qsTr("Text"))
//     messageBox.showError(qsTr("Title"), qsTr("Text"))
import QtQuick
import QtQuick.Controls.Basic

StyledDialog {
    id: control
    property string message
    property var queue: [] // pending {title, text, isError}

    function showInfo(title, text) {
        show(title, text, false);
    }
    function showError(title, text) {
        show(title, text, true);
    }
    function show(title, text, isError) {
        if (control.visible) {
            control.queue = control.queue.concat([{ title: title, text: text, isError: isError }]);
            return;
        }
        control.title = isError ? "⚠ " + title : title;
        control.message = text;
        control.open();
    }

    onClosed: {
        if (control.queue.length === 0)
            return;
        const next = control.queue[0];
        control.queue = control.queue.slice(1);
        // Let the closing popup finish before reopening it.
        Qt.callLater(control.show, next.title, next.text, next.isError);
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
