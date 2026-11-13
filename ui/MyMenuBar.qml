pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
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

    // ── Styled building blocks (only used by this menu bar) ──────────────

    // Shared metrics so every row (menu items and checkbox rows) lines up.
    readonly property int rowHeight: 30
    readonly property int rowRadius: 6
    readonly property int indicatorSize: 16

    function tint(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    // Drop-down / sub-menu container: flat card with border and soft shadow.
    component StyledMenu: Menu {
        padding: 4
        overlap: 4
        // In-scene popup so the custom shadow and radius render identically on
        // every platform (a native window popup would clip the shadow).
        popupType: Popup.Item
        delegate: StyledMenuItem {} // entries created for sub-menus

        background: Rectangle {
            implicitWidth: 220
            implicitHeight: menuBar.rowHeight
            color: Colors.base
            border.color: Colors.surface1
            border.width: 1
            radius: 8
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.35
                shadowBlur: 0.6
                shadowVerticalOffset: 3
            }
        }
    }

    // Regular entry.  Checkable entries are exclusive choices (theme, language,
    // backend, project): a ✓ in the accent colour marks the active one.
    component StyledMenuItem: MenuItem {
        id: item
        implicitHeight: menuBar.rowHeight
        leftPadding: 10
        rightPadding: 10
        spacing: 8

        indicator: Text {
            x: item.leftPadding
            y: (item.height - height) / 2
            width: menuBar.indicatorSize
            visible: item.checkable
            text: item.checked ? "✓" : ""
            color: Colors.primary
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        arrow: Text {
            x: item.width - width - item.rightPadding
            y: (item.height - height) / 2
            visible: item.subMenu !== null
            text: "›"
            color: item.highlighted ? Colors.text : Colors.overlay1
            font.pixelSize: item.font.pixelSize > 0 ? item.font.pixelSize * 1.4 : 18
        }

        contentItem: Text {
            leftPadding: item.checkable ? menuBar.indicatorSize + item.spacing : 0
            rightPadding: item.subMenu ? item.arrow.width + item.spacing : 0
            text: item.text
            font: item.font
            color: item.enabled ? Colors.text : Colors.overlay1
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            implicitWidth: 200
            implicitHeight: menuBar.rowHeight
            radius: menuBar.rowRadius
            color: !item.enabled ? "transparent"
                 : item.down ? Colors.surface1
                 : item.highlighted ? Colors.surface0 : "transparent"
        }
    }

    // Multi-select row (projects / models to delete).  Not a MenuItem, so
    // clicking it toggles the box without closing the menu.
    component MenuCheckRow: AbstractButton {
        id: row
        property bool isChecked: false
        implicitWidth: Math.max(200, rowLabel.implicitWidth + leftPadding + rightPadding
                                + menuBar.indicatorSize + spacing)
        implicitHeight: menuBar.rowHeight
        leftPadding: 10
        rightPadding: 10
        spacing: 8
        hoverEnabled: true

        contentItem: RowLayout {
            spacing: row.spacing
            Rectangle {
                implicitWidth: menuBar.indicatorSize - 2
                implicitHeight: menuBar.indicatorSize - 2
                Layout.leftMargin: 1
                radius: 4
                color: row.isChecked ? Colors.primary : "transparent"
                border.color: row.isChecked ? Colors.primary : Colors.overlay1
                border.width: 1.5
                Text {
                    anchors.centerIn: parent
                    visible: row.isChecked
                    text: "✓"
                    color: Colors.mantle
                    font.bold: true
                    font.pixelSize: parent.height - 3
                }
            }
            Text {
                id: rowLabel
                Layout.fillWidth: true
                text: row.text
                font: row.font
                color: Colors.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        background: Rectangle {
            radius: menuBar.rowRadius
            color: row.down ? Colors.surface1 : row.hovered ? Colors.surface0 : "transparent"
        }
    }

    component StyledSeparator: MenuSeparator {
        topPadding: 4
        bottomPadding: 4
        leftPadding: 8
        rightPadding: 8
        contentItem: Rectangle {
            implicitWidth: 180
            implicitHeight: 1
            color: Colors.surface0
        }
    }

    // ── Bar ──────────────────────────────────────────────────────────────

    leftPadding: 6
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
            radius: menuBar.rowRadius
            color: barItem.opened ? menuBar.tint(Colors.primary, 0.15)
                 : barItem.highlighted || barItem.hovered ? Colors.surface0
                 : "transparent"
        }
    }

    background: Rectangle {
        implicitHeight: 30
        color: Colors.mantle
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Colors.surface0
        }
    }

    // ── Menus ────────────────────────────────────────────────────────────

    StyledMenu {
        title: qsTr("File")
        StyledMenuItem {
            text: qsTr("New Project")
            onTriggered: menuBar.requestNewProject()
        }
        StyledMenu {
            title: qsTr("Open Project...")
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
        StyledMenu {
            title: qsTr("Delete Project...")
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
            StyledSeparator {}
            StyledMenuItem {
                text: qsTr("Delete Selected")
                enabled: menuBar.mainWin.selectedProjects.length > 0
                onTriggered: menuBar.requestDeleteProject()
            }
        }
        StyledMenuItem {
            text: qsTr("Clean Project")
            onTriggered: menuBar.requestCleanProject()
        }
        StyledSeparator {}
        StyledMenuItem {
            text: qsTr("Exit")
            onTriggered: Qt.quit()
        }
    }

    StyledMenu {
        title: qsTr("Settings")
        StyledMenu {
            title: qsTr("Themes")
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
            title: qsTr("Languages")
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
        StyledMenu {
            title: qsTr("Backend")
            StyledMenuItem {
                text: qsTr("Add Backend...")
                onTriggered: backendDialog.open()
            }
            StyledSeparator {}
            StyledMenu {
                title: qsTr("Change Backend")
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
        }
        StyledMenu {
            title: qsTr("Local Models")
            StyledMenuItem {
                text: qsTr("Add Model...")
                onTriggered: localModelDialog.open()
            }
            StyledSeparator {}
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
            StyledSeparator {}
            StyledMenuItem {
                text: qsTr("Delete Selected")
                enabled: menuBar.mainWin.selectedModels.length > 0
                onTriggered: menuBar.requestDeleteModels()
            }
        }
        StyledMenuItem {
            text: qsTr("OpenAlex API key")
            onTriggered: menuBar.requestOpenAlexApiKey()
        }
    }
}
