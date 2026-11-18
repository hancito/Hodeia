pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

MenuBar {
    id: menuBar
    property var mainWin: null
    signal requestNewProject
    signal requestSwitchProject(string projectName)
    signal requestDeleteProject
    signal requestClearProject
    signal requestDeleteModels
    signal requestOpenAlexApiKey
    signal requestAbout
    signal requestZotero

    // ── Bar ──────────────────────────────────────────────────────────────

    leftPadding: Style.paneMargin - 4
    spacing: 2

    delegate: MenuBarItem {
        id: barItem
        readonly property bool opened: barItem.menu && barItem.menu.visible
        leftPadding: 10
        rightPadding: 10
        topPadding: 4
        bottomPadding: 4
        topInset: 3
        bottomInset: 3

        contentItem: Text {
            text: barItem.text
            font: barItem.font
            color: barItem.opened ? Colors.primary : Colors.text
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            implicitHeight: 24
            radius: Style.radiusSmall
            color: barItem.opened ? Style.tint(Colors.primary, 0.15)
                 : barItem.highlighted || barItem.hovered ? Style.tint(Colors.text, 0.08)
                 : "transparent"
        }
    }

    // Same fill as the toolbar below: menu bar + toolbar read as one header
    // block, separated from the content by the toolbar's bottom line.
    background: Rectangle {
        implicitHeight: 30
        color: Colors.mantle
    }

    // ── Menus ────────────────────────────────────────────────────────────
    // One menu per domain: Project (data), Models (inference), Settings
    // (preferences and external services). "..." only on items opening a
    // dialog; destructive items close their group.

    StyledMenu {
        title: qsTr("Project")
        StyledMenuItem {
            text: qsTr("New Project...")
            onTriggered: menuBar.requestNewProject()
        }
        StyledMenu {
            title: qsTr("Open Project")
            Repeater {
                model: configManager.projectList
                delegate: StyledMenuItem {
                    required property var modelData
                    text: modelData
                    checkable: true
                    checked: configManager.lastProject === modelData
                    onTriggered: {
                        menuBar.requestSwitchProject(modelData);
                    }
                }
            }
        }
        StyledMenuSeparator {}
        StyledMenuItem {
            text: qsTr("Zotero...")
            onTriggered: menuBar.requestZotero()
        }
        StyledMenuSeparator {}
        StyledMenuItem {
            text: qsTr("Clear Project Data...")
            destructive: true
            onTriggered: menuBar.requestClearProject()
        }
        StyledMenu {
            title: qsTr("Delete Projects")
            Repeater {
                model: configManager.projectList
                delegate: MenuCheckRow {
                    required property var modelData
                    text: modelData
                    isChecked: menuBar.mainWin.selectedProjects.includes(modelData)
                    onClicked: {
                        const sel = menuBar.mainWin.selectedProjects;
                        menuBar.mainWin.selectedProjects = sel.includes(modelData)
                            ? sel.filter(p => p !== modelData)
                            : sel.concat([modelData]);
                    }
                }
            }
            StyledMenuSeparator {}
            StyledMenuItem {
                text: qsTr("Delete Selected")
                destructive: true
                enabled: menuBar.mainWin.selectedProjects.length > 0
                onTriggered: menuBar.requestDeleteProject()
            }
        }
        StyledMenuSeparator {}
        StyledMenuItem {
            text: qsTr("Quit")
            onTriggered: Qt.quit()
        }
    }

    StyledMenu {
        title: qsTr("Models")
        StyledMenu {
            title: qsTr("Backend")
            Repeater {
                model: configManager.backendList
                delegate: StyledMenuItem {
                    required property var modelData
                    text: modelData.name
                    checkable: true
                    checked: configManager.activeBackend === modelData.name
                    onTriggered: {
                        configManager.setActiveBackend(modelData.name);
                    }
                }
            }
        }
        StyledMenuItem {
            text: qsTr("Manage Backends...")
            onTriggered: backendDialog.open()
        }
        StyledMenuSeparator {}
        StyledMenuItem {
            text: qsTr("Add Local Model...")
            onTriggered: localModelDialog.open()
        }
        StyledMenu {
            title: qsTr("Delete Local Models")
            Repeater {
                model: helpers.localModelList
                delegate: MenuCheckRow {
                    required property var modelData
                    text: modelData
                    isChecked: menuBar.mainWin.selectedModels.includes(modelData)
                    onClicked: {
                        const sel = menuBar.mainWin.selectedModels;
                        menuBar.mainWin.selectedModels = sel.includes(modelData)
                            ? sel.filter(p => p !== modelData)
                            : sel.concat([modelData]);
                    }
                }
            }
            StyledMenuSeparator {}
            StyledMenuItem {
                text: qsTr("Delete Selected")
                destructive: true
                enabled: menuBar.mainWin.selectedModels.length > 0
                onTriggered: menuBar.requestDeleteModels()
            }
        }
    }

    StyledMenu {
        title: qsTr("Settings")
        StyledMenu {
            title: qsTr("Theme")
            Repeater {
                model: themeManager.themes
                delegate: StyledMenuItem {
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
        StyledMenu {
            title: qsTr("Language")
            StyledMenuItem {
                text: qsTr("English")
                checkable: true
                checked: configManager.language === "en"
                onTriggered: configManager.language = "en"
            }
            StyledMenuItem {
                text: qsTr("Français")
                checkable: true
                checked: configManager.language === "fr"
                onTriggered: configManager.language = "fr"
            }
        }
        StyledMenuSeparator {}
        StyledMenuItem {
            text: qsTr("OpenAlex API Key...")
            onTriggered: menuBar.requestOpenAlexApiKey()
        }
    }

    StyledMenu {
        title: qsTr("Help")
        StyledMenuItem {
            text: qsTr("About HodeIA...")
            onTriggered: menuBar.requestAbout()
        }
    }
}
