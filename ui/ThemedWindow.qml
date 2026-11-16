// Base for every application window: UI font, theme background and the
// global palette used by any control not replaced by a Styled* component
// (Label, ToolSeparator, TextArea…).  Main.qml and RagWindow.qml derive from
// it so the palette is defined once.
import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: themedWindow

    // Desired size in logical (device-independent) pixels.  The actual size is
    // clamped to the available screen area so the window still fits with
    // display scaling (e.g. 1920×1080 at 200 % only offers 960×~520 logical
    // pixels on a 1080p screen).
    property int preferredWidth: 1280
    property int preferredHeight: 800
    readonly property real screenFillRatio: 0.9

    width: Math.min(preferredWidth, Math.round(Screen.desktopAvailableWidth * screenFillRatio))
    height: Math.min(preferredHeight, Math.round(Screen.desktopAvailableHeight * screenFillRatio))
    // Must stay ≥ the sum of the SplitView pane minimums (260 + 360) + handle/margins.
    minimumWidth: Math.min(720, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(400, Screen.desktopAvailableHeight)

    Component.onCompleted: {
        // Center on the screen the window opens on (virtualX/Y handle multi-monitor).
        x = Screen.virtualX + Math.max(0, (Screen.width - width) / 2);
        y = Screen.virtualY + Math.max(0, (Screen.height - height) / 2);
    }

    font.family: Fonts.ui
    // Fixed base size: otherwise the system default applies (9 pt = 12 px on
    // Windows, often 10 pt on Linux), which looks small in the menu bar and
    // differs between platforms.  Inherited by every control and Popup.Item
    // popup (menus, dialogs, combo boxes) that does not set its own size.
    font.pixelSize: Style.fontBody

    background: Rectangle {
        color: Colors.base
    }

    palette.base: Colors.crust
    palette.alternateBase: Colors.base
    palette.window: Colors.base
    palette.windowText: Colors.text
    palette.button: Colors.base
    palette.buttonText: Colors.text
    palette.brightText: Colors.mantle
    palette.text: Colors.text
    palette.placeholderText: Colors.overlay1
    palette.light: Colors.crust
    palette.midlight: Colors.mantle
    palette.mid: Colors.surface0
    palette.dark: Colors.surface1
    palette.shadow: Colors.overlay0
    palette.highlight: Colors.primary
    palette.highlightedText: Colors.onPrimary

    // Inactive
    palette.inactive.highlight: Colors.surface1
    palette.inactive.link: Colors.surface1
    palette.inactive.linkVisited: Colors.surface1

    // Disabled
    palette.disabled.windowText: Colors.overlay1
    palette.disabled.base: Colors.base
    palette.disabled.alternateBase: Colors.base
    palette.disabled.text: Colors.overlay1
    palette.disabled.placeholderText: Colors.overlay1
    palette.disabled.button: Colors.base
    palette.disabled.buttonText: Colors.overlay1
    palette.disabled.brightText: Colors.mantle
    palette.disabled.highlight: Colors.surface2
    palette.disabled.highlightedText: Colors.surface0
    palette.disabled.link: Colors.surface0
    palette.disabled.linkVisited: Colors.surface0
}
