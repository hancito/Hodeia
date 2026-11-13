// Button in three flavours:
//   default      filled surface (secondary action)
//   highlighted  filled accent  (primary action) — or `emphasized`, which
//                does the same but is never reset by Qt (DialogButtonBox
//                forces `highlighted` off on the buttons it creates)
//   flat         transparent until hovered (toolbar / icon actions)
// `destructive` swaps the accent for red (delete, stop…).
// `toolTip` shows a StyledToolTip on hover (empty = none).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl

Button {
    id: control
    property bool destructive: false
    property bool emphasized: false
    property string toolTip: ""
    readonly property bool filled: highlighted || emphasized

    StyledToolTip {
        text: control.toolTip
        visible: control.hovered && control.toolTip !== ""
    }

    readonly property color accent: destructive ? Colors.red : Colors.primary
    readonly property color foreground: !enabled ? Colors.overlay1
                                      : filled ? (destructive ? Colors.onRed : Colors.onPrimary)
                                      : destructive ? Colors.red
                                      : Colors.text

    padding: 6
    leftPadding: 12
    rightPadding: 12
    spacing: 6
    icon.width: 20
    icon.height: 20
    icon.color: foreground

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.foreground
    }

    background: Rectangle {
        // Text buttons get a minimum width so short labels ("OK") don't
        // shrink to a square; icon-only buttons stay square.
        implicitWidth: control.text !== "" && control.display !== AbstractButton.IconOnly ? 80 : Style.controlHeight
        implicitHeight: Style.controlHeight
        radius: Style.radiusSmall
        opacity: control.enabled ? 1 : 0.5
        color: {
            if (control.filled)
                return control.down ? Qt.darker(control.accent, 1.15)
                     : control.hovered ? Qt.lighter(control.accent, 1.1)
                     : control.accent;
            if (control.flat)
                return control.down ? Style.tint(control.destructive ? Colors.red : Colors.text, 0.16)
                     : control.hovered ? Style.tint(control.destructive ? Colors.red : Colors.text, 0.08)
                     : "transparent";
            if (control.destructive && (control.hovered || control.down))
                return Style.tint(Colors.red, control.down ? 0.3 : 0.18);
            return control.down ? Colors.overlay0 : control.hovered ? Colors.surface2 : Colors.surface1;
        }
        border.width: control.visualFocus ? 1 : 0
        border.color: control.accent
    }
}
