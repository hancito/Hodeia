// Menu entry.  Checkable entries are exclusive choices (theme, language,
// backend, project): a ✓ in the accent colour marks the active one.
// `destructive` shows the entry in red (delete actions).
import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: item
    property bool destructive: false

    implicitHeight: Style.rowHeight
    leftPadding: 10
    rightPadding: 10
    spacing: 8

    indicator: Text {
        x: item.leftPadding
        y: (item.height - height) / 2
        width: Style.indicatorSize
        visible: item.checkable
        text: item.checked ? "✓" : ""
        color: Colors.primary
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
    }

    arrow: Text {
        x: item.width - width - item.rightPadding
        y: (item.height - height) / 2
        visible: item.subMenu !== null
        text: "›"
        color: item.highlighted ? Colors.text : Colors.overlay1
        font.pixelSize: Style.fontHeading + 2
    }

    contentItem: Text {
        leftPadding: item.checkable ? Style.indicatorSize + item.spacing : 0
        rightPadding: item.subMenu ? item.arrow.width + item.spacing : 0
        text: item.text
        font: item.font
        color: !item.enabled ? Colors.overlay1 : item.destructive ? Colors.red : Colors.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: Style.rowHeight
        radius: Style.radiusSmall
        color: {
            if (!item.enabled)
                return "transparent";
            const base = item.destructive ? Colors.red : Colors.text;
            return item.down ? Style.tint(base, 0.16)
                 : item.highlighted ? Style.tint(base, 0.08) : "transparent";
        }
    }
}
