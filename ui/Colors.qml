// Colors.qml — thin wrapper around themeManager.activeTheme
pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

QtObject {
    readonly property var _theme: themeManager.activeTheme || {}

    readonly property color primary: _theme.primary || "#cba6f7"
    readonly property color red: _theme.red || "#f38ba8"
    readonly property color green: _theme.green || "#a6e3a1"

    readonly property color text: _theme.text || "#cdd6f4"

    readonly property color overlay2: _theme.overlay2 || "#9399b2"
    readonly property color overlay1: _theme.overlay1 || "#7f849c"
    readonly property color overlay0: _theme.overlay0 || "#6c7086"
    readonly property color surface2: _theme.surface2 || "#585b70"
    readonly property color surface1: _theme.surface1 || "#45475a"
    readonly property color surface0: _theme.surface0 || "#313244"

    readonly property color base: _theme.base || "#1e1e2e"
    readonly property color mantle: _theme.mantle || "#181825"
    readonly property color crust: _theme.crust || "#11111b"

    // Soft blurred halo (the glow). Instantiate over the element to highlight:
    //   Loader { sourceComponent: Colors.glowHalo; onLoaded: item.source = target }
    // The consumer sets `item.source` (the element to glow).
    readonly property Component glowHalo: Component {
        MultiEffect {
            shadowEnabled: true
            shadowColor: Colors.primary
            shadowBlur: 0.01
        }
    }
}
