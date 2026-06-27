pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1920
    height: 1080

    visible: true
    font.family: Fonts.ui
    //icon.source: "qrc:/Resources/paperize.png"
    property var secondaryWindow: null
    property var windowComponent: null
    property var selectedProjects: []

    readonly property var activeTheme: themeManager.activeTheme || {}
    readonly property var activeProject: configManager.lastProject || {}
    title: qsTr("Paperize - " + activeProject)
    // Derived colors (mirrors _theme.py update_palette logic)
    readonly property color highlightedTextColor: Colors.primary === "#cba6f7" ? Colors.mantle : Colors.text

    // Global palette applied to all QtQuick.Controls
    palette.base: Colors.crust
    palette.alternateBase: Colors.base
    palette.window: Colors.base

    palette.windowText: Colors.text
    palette.button: Colors.base
    palette.buttonText: Colors.text
    palette.brightText: Colors.mantle
    palette.text: Colors.text
    palette.placeholderText: Colors.overlay1
    palette.light: Colors.crust
    palette.midlight: Colors.mantle
    palette.mid: Colors.surface0
    palette.dark: Colors.surface1
    palette.shadow: Colors.overlay0
    palette.highlight: Colors.primary
    palette.highlightedText: highlightedTextColor

    // Inactive
    palette.inactive.highlight: Colors.surface1
    palette.inactive.link: Colors.surface1
    palette.inactive.linkVisited: Colors.surface1

    // Disabled
    palette.disabled.windowText: Colors.overlay1
    palette.disabled.base: Colors.base
    palette.disabled.alternateBase: Colors.base
    palette.disabled.text: Colors.overlay1
    palette.disabled.placeholderText: Colors.overlay1
    palette.disabled.button: Colors.base
    palette.disabled.buttonText: Colors.overlay1
    palette.disabled.brightText: Colors.mantle
    palette.disabled.highlight: Colors.surface2
    palette.disabled.highlightedText: Colors.surface0
    palette.disabled.link: Colors.surface0
    palette.disabled.linkVisited: Colors.surface0

    Component.onCompleted: {
        // Load the QML file as a component
        windowComponent = Qt.createComponent("RagWindow.qml");

        // Apply saved theme index from config
        if (configManager.activeThemeIndex !== undefined && configManager.activeThemeIndex >= 0) {
            themeManager.setThemeIndex(configManager.activeThemeIndex);
        }

        // Load models for the active backend (local or remote)
        summarizer.loadActiveBackendModels();

        // Show backend setup if no backends configured
        //if (configManager.backendList.length === 0) {
        //   backendDialog.open();
        //}
    }
    menuBar: MyMenuBar {
        id: topNavigation
        mainWin: root
        onRequestNewProject: newProjectDialog.open()
        onRequestSwitchProject: function (projectName) {
            let ok = configManager.switchToProject(projectName);
            if (ok) {
                let dbPath = configManager.projectDbPath(projectName);
                databaseManager.switchDatabase(dbPath);
            }
        }
        onRequestDeleteProject: deleteConfirmDialog.open()
        onRequestCleanProject: cleanProjectDialog.open()
    }

    // Apply theme to the entire window content
    background: Rectangle {
        color: Colors.base || Colors.background || "#1e1e1e"
    }

    GridLayout {
        anchors.fill: parent
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10

        SplitView {
            id: mainSplitView
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 10
            handle: Rectangle {
                implicitWidth: 5
                color: "transparent"
                opacity: 0
            }

            LeftPane {
                id: leftPaneItem
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 400
                statusBarLabel: footerLabel
                // 2. The effect that adds the shadow

                onOpenRagWindow: {
                    // Check if the window instance already exists and is valid
                    if (root.secondaryWindow !== null) {
                        console.log("Window already open. Bringing to focus...");
                        root.secondaryWindow.raise();            // Brings the window to the top of the stack
                        root.secondaryWindow.requestActivate(); // Gives the window keyboard/mouse focus
                        return;
                    }

                    // 2. If it doesn't exist, create it
                    if (root.windowComponent.status === Component.Ready) {
                        let newWin = root.windowComponent.createObject(root);
                        if (newWin !== null) {
                            root.secondaryWindow = newWin; // Store the reference

                            // 3. Listen for the window closing so we can reset our reference to null
                            newWin.closed.connect(function () {
                                root.secondaryWindow = null;
                                console.log("Window closed. Reference cleared.");
                            });
                        }
                    } else {
                        console.log("Component not ready:", root.windowComponent.errorString());
                    }
                }
            }

            CentralPane {
                id: centralPaneItem
                SplitView.minimumWidth: 1000
            }

            RightPane {
                id: rightPaneItem
                SplitView.minimumWidth: 600
            }
        }
    }

    // New Project input dialog
    Dialog {
        id: newProjectDialog
        anchors.centerIn: parent
        modal: true
        title: "New Project"
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: "Enter the Project Name"
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "Type here..."
                focus: true
                onAccepted: doCreateProject()
            }
            Label {
                id: messageProjectName
                text: "Please enter a name or cancel"
                visible: false
                horizontalAlignment: Text.AlignHCenter
                color: Colors.red
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Create"
                    highlighted: true
                    onClicked: root.doCreateProject()
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Cancel"
                    onClicked: {
                        newProjectDialog.close();
                        nameField.clear();
                        messageProjectName.visible = false;
                    }
                }
            }
        }
        onClosed: {
            nameField.clear();
            messageProjectName.visible = false;
        }
    }

    function doCreateProject() {
        let name = nameField.text.trim();
        if (name === "") {
            messageProjectName.visible = true;
            return;
        }
        let ok = configManager.createProject(name);
        if (ok) {
            let dbPath = configManager.projectDbPath(name);
            databaseManager.switchDatabase(dbPath);
            newProjectDialog.close();
        }
    }

    // Clean project confirmation dialog
    Dialog {
        id: cleanProjectDialog
        anchors.centerIn: parent
        modal: true
        title: "Clean Project"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            text: "Clean project \"" + activeProject + "\"? All files and data will be permanently removed."
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            let ok = configManager.resetActiveProject();
            if (ok) {
                let dbPath = configManager.projectDbPath(activeProject);
                databaseManager.switchDatabase(dbPath);
            }
        }
    }

    // Delete project confirmation dialog
    Dialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        modal: true
        title: "Delete Projects"
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            spacing: 5
            Label {
                text: "Delete the selected project(s)? This cannot be undone."
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.selectedProjects
                delegate: Label {
                    required property var modelData
                    text: "  • " + modelData
                    font.weight: Font.Bold
                }
            }
        }
        onAccepted: {
            configManager.deleteProjects(root.selectedProjects);
            // Switch DB to the new active project (set automatically by C++)
            let newActive = configManager.lastProject;
            let dbPath = configManager.projectDbPath(newActive);
            databaseManager.switchDatabase(dbPath);
            root.selectedProjects = [];
        }
        onRejected: {
            root.selectedProjects = [];
        }
    }

    // Backend setup modal
    BackendWindow {
        id: backendDialog
    }

    // File dialog for selecting GGUF summarizer models
    FileDialog {
        id: localModelFileDialog
        title: "Select GGUF Model File"
        fileMode: FileDialog.OpenFile
        nameFilters: ["GGUF files (*.gguf)", "All files (*)"]
        onAccepted: {
            configManager.addLocalSummarizer(selectedFile);
        }
    }

    // Model copy progress in footer
    Connections {
        target: configManager
        function onModelCopyProgress(percentage, fileName) {
            footerLabel.text = "Copying model: " + fileName + " — " + percentage + "%";
        }
        function onModelCopyFinished(success, destPath, error) {
            if (success) {
                footerLabel.text = "Model copied: " + destPath;
            } else {
                footerLabel.text = "Copy failed: " + error;
            }
        }
    }

    // Status bar
    footer: ToolBar {
        height: 30
        Label {
            id: footerLabel
            anchors.fill: parent
            font.family: Fonts.mono
            verticalAlignment: Text.AlignLeft
            horizontalAlignment: Text.AlignLeft
            text: qsTr(activeProject + " -- Ready")
        }
    }
}
