import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    leftPadding: 10
    rightPadding: 10 + indicator.width + spacing
    spacing: 6

    delegate: ItemDelegate {
        id: entry
        required property int index
        // Leave room for the scroll bar (plus a small gap) when the list is
        // long enough to scroll, so it never covers the text.
        width: popupList.width - (popupBar.size < 1 ? popupBar.width + 4 : 0)
        implicitHeight: Style.rowHeight
        leftPadding: 10
        rightPadding: 10
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        contentItem: Text {
            text: control.textAt(entry.index)
            font: control.font
            color: entry.index === control.currentIndex ? Colors.primary : Colors.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Style.radiusSmall
            color: entry.down ? Colors.surface1 : entry.highlighted ? Colors.surface0 : "transparent"
        }
    }

    indicator: Text {
        x: control.mirrored ? control.leftPadding : control.width - width - 10
        y: (control.height - height) / 2
        text: "▾"
        color: control.enabled ? Colors.overlay2 : Colors.overlay0
        font.pixelSize: Style.fontSmall
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Colors.text : Colors.overlay1
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: Style.controlHeight
        radius: Style.radiusSmall
        color: Colors.mantle
        opacity: control.enabled ? 1 : 0.5
        border.width: 1
        border.color: control.popup.visible || control.visualFocus ? Colors.primary
                    : control.hovered ? Colors.overlay0 : Colors.surface1
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 400)
        padding: 4
        popupType: Popup.Item

        contentItem: ListView {
            id: popupList
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: StyledScrollBar {
                id: popupBar
            }
        }

        background: PopupBackground {}
    }
}
