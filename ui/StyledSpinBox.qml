import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: control
    padding: 0
    leftPadding: (mirrored ? up.indicator.width : down.indicator.width)
    rightPadding: (mirrored ? down.indicator.width : up.indicator.width)

    contentItem: TextInput {
        text: control.displayText
        clip: width < implicitWidth
        padding: 6
        font: control.font
        color: control.enabled ? Colors.text : Colors.overlay1
        selectionColor: Colors.primary
        selectedTextColor: Colors.onPrimary
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
    }

    component StepButton: Rectangle {
        id: step
        property string glyph
        property bool pressed: false
        property bool hovered: false
        implicitWidth: 28
        implicitHeight: Style.controlHeight
        height: control.height
        radius: Style.radiusSmall
        color: pressed ? Style.tint(Colors.text, 0.16) : hovered ? Style.tint(Colors.text, 0.08) : "transparent"
        Text {
            anchors.centerIn: parent
            text: step.glyph
            color: step.enabled ? Colors.text : Colors.overlay0
            font.pixelSize: Style.fontHeading
        }
    }

    up.indicator: StepButton {
        x: control.mirrored ? 0 : control.width - width
        glyph: "+"
        pressed: control.up.pressed
        hovered: control.up.hovered
    }

    down.indicator: StepButton {
        x: control.mirrored ? control.width - width : 0
        glyph: "−"
        pressed: control.down.pressed
        hovered: control.down.hovered
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Style.controlHeight
        radius: Style.radiusSmall
        color: Colors.mantle
        opacity: control.enabled ? 1 : 0.5
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Colors.primary
                    : control.hovered ? Colors.overlay0 : Colors.surface1
    }
}
