// Style.qml — shared metrics for every styled control (colours live in
// Colors.qml).  Change a value here and the whole UI follows.
pragma Singleton
import QtQuick

QtObject {
    // Corners
    readonly property int radiusSmall: 6 // buttons, inputs, menu rows, cards
    readonly property int radiusLarge: 8 // panes, popups, dialogs

    // Sizes
    readonly property int controlHeight: 32 // buttons, inputs, combo boxes
    readonly property int rowHeight: 30     // menu / combo popup rows
    readonly property int indicatorSize: 16 // check boxes, menu checkmarks

    // Spacing
    readonly property int spacing: 6
    readonly property int paneMargin: 10 // pane padding and gap between panes

    // Font sizes (pixels)
    readonly property int fontSmall: 12   // metadata, mono details, status bar
    readonly property int fontBody: 14    // empty-state messages
    readonly property int fontHeading: 16 // dialog titles
    readonly property int fontReading: 16 // summaries, answers, references
    readonly property int fontTitle: 21  // pane titles

    // `c` with alpha `a`
    function tint(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }
}
