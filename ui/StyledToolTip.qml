// Tooltip with the popup look.  StyledButton shows one through its
// `toolTip` property; the attached ToolTip (ToolTip.text) keeps the Basic
// style and can't be restyled per app, so prefer this one.
import QtQuick
import QtQuick.Controls.Basic

ToolTip {
    id: control
    delay: 400
    padding: 6
    leftPadding: 10
    rightPadding: 10
    popupType: Popup.Item

    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: Colors.text
    }

    background: Rectangle {
        color: Colors.mantle
        border.color: Colors.surface1
        radius: Style.radiusSmall
    }
}
