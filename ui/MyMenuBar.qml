pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

MenuBar {
    // 1. Create a property that main.qml can look at

    id: menuBar
        Menu {
            title: "File"
            MenuItem {
                text: "New Project"
                onTriggered: console.log("New Project")
            }
            Menu {
                title: "Open Project"
                MenuItem {
                    text: "Default"
                    onTriggered: console.log("Open Default Project")
                }
            }
            MenuItem {
                text: "Exit"
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: "Settings"
            Menu {
                title: "Themes"
                Repeater {
                    id: themesRepeater
                    model: themeManager.themes
                    delegate: MenuItem {
                        required property int index
                        required property var modelData
                        text: modelData.name
                        checkable: true
                        checked: themeManager.activeTheme.name === modelData.name
                        onTriggered: {
                            themeManager.setThemeIndex(index);
                            configManager.activeThemeIndex = index;
                        }
                    }
                }
            }
            MenuItem {
                text: "Add Backend"
                onTriggered: console.log("Add Backend")
            }
            Menu {
                title: "Change Backend"
            }
        }
    }

