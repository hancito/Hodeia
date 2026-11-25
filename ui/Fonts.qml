// Fonts.qml — custom font family names for the application
pragma Singleton
import QtQuick

QtObject {
    readonly property string ui: "Google Sans"
    readonly property string mono: "Google Sans Code"
    // Reading text (summary, query answers).  Not Lato: with Qt 6.12, as soon
    // as a rich-text document needs a fallback glyph Lato lacks (β, ≤, ∑…),
    // its first paragraph renders as garbage glyphs.
    readonly property string text: "Google Sans"
}
