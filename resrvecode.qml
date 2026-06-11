

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
