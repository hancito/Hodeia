import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: control
    padding: 4
    spacing: 8

    indicator: CheckIndicator {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        checked: control.checked
        hovered: control.hovered
        opacity: control.enabled ? 1 : 0.5
    }

    contentItem: Text {
        leftPadding: control.indicator.width + (control.text ? control.spacing : 0)
        text: control.text
        font: control.font
        color: control.enabled ? Colors.text : Colors.overlay1
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
