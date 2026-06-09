import QtQuick
import QtQuick.Controls

MenuBar {
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
                // Theme actions would be added here
            }
            MenuItem {
                text: "Add Backend"
                onTriggered: console.log("Add Backend")
            }
            Menu {
                title: "Change Backend"
                // Backend actions would be added here
            }
        }
    }

    