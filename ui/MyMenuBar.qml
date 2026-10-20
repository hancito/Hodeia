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
        title: qsTr("File")
        MenuItem {
            text: qsTr("New Project")
            onTriggered: menuBar.requestNewProject()
        }
        Menu {
            title: qsTr("Open Project...")
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
            title: qsTr("Delete Project...")
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
                text: qsTr("Delete Selected")
                enabled: mainWin.selectedProjects.length > 0
                onTriggered: menuBar.requestDeleteProject()
            }
        }
        MenuItem {
            text: qsTr("Clean Project")
            onTriggered: menuBar.requestCleanProject()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Exit")
            onTriggered: Qt.quit()
        }
    }

    Menu {
        title: qsTr("Settings")
        Menu {
            title: qsTr("Themes")
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
            title: qsTr("Languages")
            MenuItem {
                text: qsTr("English")
                checkable: true
                checked: configManager.language === "en"
                onTriggered: configManager.language = "en"
            }
            MenuItem {
                text: qsTr("Français")
                checkable: true
                checked: configManager.language === "fr"
                onTriggered: configManager.language = "fr"
            }
        }
        Menu {
            title: qsTr("Backend")
            MenuItem {
                text: qsTr("Add Backend...")
                onTriggered: backendDialog.open()
            }

            MenuSeparator {}
            Menu {
                title: qsTr("Change Backend")
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
        Menu {
            id: localModelMenu
            title: qsTr("Local Models")
            width: 280
            MenuItem {
                text: qsTr("Add Model...")
                onTriggered: localModelDialog.open()
            }
            MenuSeparator {}
            Repeater {
                model: helpers.localModelList
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
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Delete Selected")
                enabled: mainWin.selectedModels.length > 0
                onTriggered: menuBar.requestDeleteModels()
            }
        }
        MenuItem {
            text: qsTr("OpenAlex API key")
            onTriggered: menuBar.requestOpenAlexApiKey()
        }
    }
}
