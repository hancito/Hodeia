// Drop-down / sub-menu container.
import QtQuick
import QtQuick.Controls.Basic

Menu {
    padding: 4
    overlap: 4
    // In-scene popup so the custom shadow and radius render identically on
    // every platform (a native window popup would clip the shadow).
    popupType: Popup.Item
    delegate: StyledMenuItem {} // entries created for sub-menus

    background: PopupBackground {
        implicitWidth: 220
    }
}
