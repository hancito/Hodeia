pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Controls
import QtQuick.Layouts

MenuBar {
    id: menuBar
    property var mainWin: null
    signal requestNewProject
    signal requestSwitchProject(string projectName)
    signal requestDeleteProject
    signal requestCleanProject
    signal requestDeleteModels
    signal requestOpenAlexApiKey
    Menu {
        title: "File"
        MenuItem {
            text: "New Project"
            onTriggered: menuBar.requestNewProject()
        }
        Menu {
            title: "Open Project..."
            Repeater {
                model: configManager.projectList
                delegate: MenuItem {
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
        Menu {
            id: deleteProjectMenu
            title: "Delete Project..."
            Repeater {
                model: configManager.projectList
                delegate: Item {
                    required property var modelData
                    width: deleteProjectMenu.width
                    height: 32
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        CheckBox {
                            checked: mainWin.selectedProjects.includes(modelData)
                            onClicked: {
                                if (mainWin.selectedProjects.includes(modelData)) {
                                    mainWin.selectedProjects = mainWin.selectedProjects.filter(p => p !== modelData);
                                } else {
                                    mainWin.selectedProjects = mainWin.selectedProjects.concat([modelData]);
                                }
                            }
                        }
                        Label {
                            text: modelData
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            MenuItem {
                text: "Delete Selected"
                enabled: mainWin.selectedProjects.length > 0
                onTriggered: menuBar.requestDeleteProject()
            }
        }
        MenuItem {
            text: "Clean Project"
            onTriggered: menuBar.requestCleanProject()
        }
        MenuSeparator {}
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
        Menu {
            title: "Backend"
            MenuItem {
                text: "Add Backend..."
                onTriggered: backendDialog.open()
            }

            MenuSeparator {}
            Menu {
                title: "Change Backend"
                Repeater {
                    model: configManager.backendList
                    delegate: MenuItem {
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
        }
        MenuItem {
            text: "Add Local Model"
            onTriggered: localModelDialog.open()
        }
        Menu {
            id: deleteModelMenu
            title: "Delete Local Model..."
            width: 280
            Repeater {
                model: modelHelper.localModelList
                delegate: Item {
                    required property var modelData
                    width: contentItem.implicitWidth
                    height: contentItem.implicitHeight
                    RowLayout {
                        id: contentItem
                        anchors.centerIn: parent
                        anchors.margins: 4
                        CheckBox {
                            checked: mainWin.selectedModels.includes(modelData)
                            onClicked: {
                                if (mainWin.selectedModels.includes(modelData)) {
                                    mainWin.selectedModels = mainWin.selectedModels.filter(p => p !== modelData);
                                } else {
                                    mainWin.selectedModels = mainWin.selectedModels.concat([modelData]);
                                }
                            }
                        }
                        Label {
                            text: modelData
                        }
                    }
                }
            }
            MenuItem {
                text: "Delete Selected"
                enabled: mainWin.selectedModels.length > 0
                onTriggered: menuBar.requestDeleteModels()
            }
        }
        MenuItem {
            text: "Add or Modify OpenAlex API key"
            onTriggered: menuBar.requestOpenAlexApiKey()
        }
    }
}
