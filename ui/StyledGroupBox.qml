// Group box: bold title above a rounded, bordered frame.
import QtQuick
import QtQuick.Controls.Basic

GroupBox {
    id: control
    padding: 12
    spacing: 8
    topPadding: padding + (implicitLabelWidth > 0 ? implicitLabelHeight + spacing : 0)

    label: Text {
        x: control.leftPadding
        width: control.availableWidth
        text: control.title
        font.family: control.font.family
        font.bold: true
        color: Colors.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        y: control.topPadding - control.bottomPadding
        width: parent.width
        height: parent.height - control.topPadding + control.bottomPadding
        radius: Style.radiusLarge
        color: "transparent"
        border.color: Colors.surface1
    }
}
