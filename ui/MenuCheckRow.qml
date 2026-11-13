// Multi-select menu row (projects / models to delete).  Not a MenuItem, so
// clicking it toggles the box without closing the menu.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: row
    property bool isChecked: false

    implicitWidth: Math.max(200, rowLabel.implicitWidth + leftPadding + rightPadding
                            + Style.indicatorSize + spacing)
    implicitHeight: Style.rowHeight
    leftPadding: 10
    rightPadding: 10
    spacing: 8
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: row.spacing
        CheckIndicator {
            checked: row.isChecked
            hovered: row.hovered
        }
        Text {
            id: rowLabel
            Layout.fillWidth: true
            text: row.text
            font: row.font
            color: Colors.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        radius: Style.radiusSmall
        color: row.down ? Style.tint(Colors.text, 0.16)
             : row.hovered ? Style.tint(Colors.text, 0.08) : "transparent"
    }
}
