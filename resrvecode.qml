// Theme.qml
import QtQuick

QtObject {
    // 1. Keep track of the active theme index
    property int currentThemeIndex: 0 // Default to Light

    // 2. Define your 5 distinct color palettes
    readonly property var themesList: [
        // Theme 0: Light
        {
            name: "Light",
            background: "#FFFFFF",
            text: "#111111",
            accent: "#0066CC",
            secondaryText: "#666666"
        },
        // Theme 1: Dark
        {
            name: "Dark",
            background: "#1E1E1E",
            text: "#FFFFFF",
            accent: "#3DAEE9",
            secondaryText: "#AAAAAA"
        },
        // Theme 2: Sepia (Reading Mode)
        {
            name: "Sepia",
            background: "#F4ECD8",
            text: "#433422",
            accent: "#8F6745",
            secondaryText: "#70604E"
        },
        // Theme 3: Cyberpunk / Neon
        {
            name: "Cyberpunk",
            background: "#0F1016",
            text: "#00FFCC",
            accent: "#FF0055",
            secondaryText: "#9900FF"
        },
        // Theme 4: Forest / Eco
        {
            name: "Forest",
            background: "#E8F0E8",
            text: "#1C331E",
            accent: "#2E6F40",
            secondaryText: "#557759"
        }
    ]

    // 3. Expose active properties.
    // They safely update automatically whenever currentThemeIndex changes.
    property color backgroundColor: themesList[currentThemeIndex].background
    property color textColor: themesList[currentThemeIndex].text
    property color accentColor: themesList[currentThemeIndex].accent
    property color secondaryText: themesList[currentThemeIndex].secondaryText
}


import QtQuick
import QtQuick.Controls

Window {
    width: 600
    height: 400
    visible: true
    title: "Paperize - 5 Theme Engine"

    Theme { id: appTheme }

    color: appTheme.backgroundColor

    menuBar: MenuBar {
        Menu {
            title: "View"
            Menu {
                title: "Themes"

                ActionGroup { id: themeGroup }

                // Dynamically loops through your 5 themes and creates menu actions
                Repeater {
                    model: appTheme.themesList

                    delegate: Action {
                        text: modelData.name // e.g. "Light", "Dark", etc.
                        checkable: true
                        checked: appTheme.currentThemeIndex === index
                        ActionGroup.group: themeGroup

                        onTriggered: appTheme.currentThemeIndex = index
                    }
                }
            }
        }
    }

    // Example UI elements changing colors seamlessly
    Text {
        anchors.centerIn: parent
        text: "Currently using: " + appTheme.themesList[appTheme.currentThemeIndex].name
        font.pixelSize: 24
        color: appTheme.textColor
    }
}
