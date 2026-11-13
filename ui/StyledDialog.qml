// Modal dialog: card background, bold title, right-aligned styled buttons.
// The Ok/accept button is the primary action; with `destructive: true`
// (delete, clean…) it turns red.
import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: control
    property bool destructive: false

    modal: true
    popupType: Popup.Item
    padding: 16
    topPadding: 12

    // Give every dialog an explicit width: without one, Basic's Dialog sizes
    // itself from its content and runs into an implicitWidth binding loop.
    background: PopupBackground {}

    // Short fade + zoom so the dialog doesn't just pop in.  No exit
    // transition on purpose: several dialogs switch the project database in
    // onAccepted, which must run after the popup has fully closed and given
    // focus back (see onRequestSwitchProject in Main.qml for the crash).
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 140
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
            duration: 140
            easing.type: Easing.OutCubic
        }
    }

    header: Label {
        visible: control.title !== ""
        text: control.title
        elide: Label.ElideRight
        font.bold: true
        font.pixelSize: Style.fontHeading
        color: Colors.text
        leftPadding: 16
        rightPadding: 16
        topPadding: 16
    }

    // Dialog forwards standardButtons to a DialogButtonBox footer.
    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 8
        padding: 16
        topPadding: 4
        background: Item {}
        delegate: StyledButton {
            emphasized: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
            destructive: control.destructive && DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
    }
}
