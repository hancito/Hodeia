// On/off switch in the app's style: a rounded track, the accent colour when
// on, dimmed when disabled.  `toolTip` shows a StyledToolTip on hover (also
// when disabled, to say why).
import QtQuick
import QtQuick.Controls.Basic

Switch {
    id: control
    property string toolTip: ""
    padding: 4
    spacing: 8
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 38
        implicitHeight: 20
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Colors.primary : Colors.surface1
        border.width: 1
        border.color: control.checked ? Colors.primary : control.hovered ? Colors.overlay2 : Colors.overlay1
        opacity: control.enabled ? 1 : 0.4

        Rectangle {
            width: parent.height - 6
            height: width
            radius: width / 2
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? Colors.onPrimary : Colors.overlay2
            Behavior on x {
                NumberAnimation {
                    duration: 120
                }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + (control.text ? control.spacing : 0)
        text: control.text
        font: control.font
        color: control.enabled ? Colors.text : Colors.overlay1
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    // A disabled control receives no hover: a HoverHandler still does.
    HoverHandler {
        id: hover
    }
    StyledToolTip {
        visible: control.toolTip !== "" && hover.hovered
        text: control.toolTip
    }
}
