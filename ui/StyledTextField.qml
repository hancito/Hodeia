import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    leftPadding: 10
    rightPadding: 10
    color: Colors.text
    placeholderTextColor: Colors.overlay1
    selectionColor: Colors.primary
    selectedTextColor: Colors.onPrimary

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: Style.controlHeight
        radius: Style.radiusSmall
        color: Colors.mantle
        opacity: control.enabled ? 1 : 0.5
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Colors.primary
                    : control.hovered ? Colors.overlay0 : Colors.surface1
    }
}
