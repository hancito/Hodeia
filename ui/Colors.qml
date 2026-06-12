// Colors.qml — thin wrapper around themeManager.activeTheme
pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    readonly property var _theme: themeManager.activeTheme || {}

    readonly property color primary: _theme.primary || "#cba6f7"
    readonly property color secondary: _theme.secondary || "#89b4fa"
    readonly property color magenta: _theme.magenta || "#cba6f7"
    readonly property color red: _theme.red || "#f38ba8"
    readonly property color orange: _theme.orange || "#fab387"
    readonly property color yellow: _theme.yellow || "#f9e2af"
    readonly property color green: _theme.green || "#a6e3a1"
    readonly property color cyan: _theme.cyan || "#89dceb"
    readonly property color blue: _theme.blue || "#89b4fa"

    readonly property color text: _theme.text || "#cdd6f4"
    readonly property color subtext1: _theme.subtext1 || "#bac2de"
    readonly property color subtext0: _theme.subtext0 || "#a6adc8"

    readonly property color overlay2: _theme.overlay2 || "#9399b2"
    readonly property color overlay1: _theme.overlay1 || "#7f849c"
    readonly property color overlay0: _theme.overlay0 || "#6c7086"
    readonly property color surface2: _theme.surface2 || "#585b70"
    readonly property color surface1: _theme.surface1 || "#45475a"
    readonly property color surface0: _theme.surface0 || "#313244"

    readonly property color base: _theme.base || "#1e1e2e"
    readonly property color mantle: _theme.mantle || "#181825"
    readonly property color crust: _theme.crust || "#11111b"
}
