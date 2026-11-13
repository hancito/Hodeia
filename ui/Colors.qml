// Colors.qml — thin wrapper around themeManager.activeTheme
pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick

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

    // Text / icons drawn on an accent fill: whichever of crust (the darkest
    // shade in dark themes, the lightest in light themes) and text contrasts
    // more with the fill, so it stays readable whatever the theme.
    readonly property color onPrimary: onColor(primary)
    readonly property color onRed: onColor(red)

    // WCAG relative luminance and contrast ratio.
    function luminance(c) {
        function lin(v) {
            return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
    }
    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
    }
    function onColor(fill) {
        return contrast(crust, fill) >= contrast(text, fill) ? crust : text;
    }
}
