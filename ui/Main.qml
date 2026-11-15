pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ThemedWindow {
    id: root
    width: 1920
    height: 1080

    visible: true
    //icon.source: "qrc:/Resources/hodeia.png"
    property var secondaryWindow: null
    property var windowComponent: null
    property var selectedProjects: []
    property var selectedModels: []

    readonly property var activeProject: configManager.lastProject || {}
    title: qsTr("Hodeia - %1").arg(activeProject)

    Component.onCompleted: {
        // Load the QML file as a component
        windowComponent = Qt.createComponent("RagWindow.qml");

        // Apply saved theme index from config
        if (configManager.activeThemeIndex !== undefined && configManager.activeThemeIndex >= 0) {
            themeManager.setThemeIndex(configManager.activeThemeIndex);
        }

        // Show backend setup if no backends configured
        //if (configManager.backendList.length === 0) {
        //   backendDialog.open();
        //}
    }

    onClosing: {
        // Stop any in-flight operations before exiting
        if (root.secondaryWindow !== null) {
            root.secondaryWindow.close();
        }
        engineController.stopEngine();
        summaryController.stopSummarize();
        queryController.stopQuery();
    }

    menuBar: MyMenuBar {
        id: topNavigation
        mainWin: root
        onRequestNewProject: newProjectDialog.open()
        onRequestSwitchProject: function (projectName) {
            // Deferred: this runs from MenuItem.onTriggered, BEFORE the menu
            // closes.  switchDatabase() resets the models, which destroys the
            // view delegates — including the item the menu restores focus to on
            // close → SEGV in QQuickDeliveryAgentPrivate::setFocusInScope.
            // Qt.callLater lets the menu finish closing first.
            Qt.callLater(function () {
                let ok = configManager.switchToProject(projectName);
                if (ok) {
                    let dbPath = configManager.projectDbPath(projectName);
                    databaseManager.switchDatabase(dbPath);
                }
            });
        }
        onRequestDeleteProject: deleteConfirmDialog.open()
        onRequestCleanProject: cleanProjectDialog.open()
        onRequestDeleteModels: deleteModelDialog.open()
        onRequestOpenAlexApiKey: openAlexKeyDialog.open()
    }

    header: MyToolBar {
        id: toolBarContainer
        statusBarLabel: footerLabel

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
                console.warn("Component not ready:", root.windowComponent.errorString());
                messageBox.showError(qsTr("Cannot Open Query Window"), root.windowComponent.errorString());
            }
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin

        SplitView {
            id: mainSplitView
            Layout.fillHeight: true
            Layout.fillWidth: true
            handle: SplitHandleBar {}

            SummaryDbPane {
                id: summaryDbPaneItem
                SplitView.minimumWidth: 600
                statusBarLabel: footerLabel
            }

            DisplayPane {
                id: displayPaneItem
                SplitView.minimumWidth: 1000
                activePaperHash: summaryDbPaneItem.activePaperHash
            }
        }
    }

    // New Project input dialog
    StyledDialog {
        id: newProjectDialog
        anchors.centerIn: parent
        width: 420
        title: qsTr("New Project")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Component.onCompleted: {
            const create = standardButton(Dialog.Ok);
            create.text = qsTr("Create");
            create.enabled = Qt.binding(() => nameField.text.trim() !== "");
        }
        onAboutToShow: {
            nameField.clear();
            messageProjectName.visible = false;
        }
        onAccepted: root.doCreateProject()
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: qsTr("Enter the Project Name")
            }
            StyledTextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Type here...")
                focus: true
                onAccepted: {
                    if (text.trim() === "")
                        messageProjectName.visible = true;
                    else
                        newProjectDialog.accept();
                }
            }
            Label {
                id: messageProjectName
                text: qsTr("Please enter a name or cancel")
                visible: false
                horizontalAlignment: Text.AlignHCenter
                color: Colors.red
            }
        }
    }

    function doCreateProject() {
        let name = nameField.text.trim();
        if (name === "") {
            messageProjectName.visible = true;
            return;
        }
        let existing = configManager.projectList;
        if (existing.includes(name)) {
            existingProjectName = name;
            existingProjectDialog.open();
            return;
        }
        let ok = configManager.createProject(name);
        if (ok) {
            let dbPath = configManager.projectDbPath(name);
            databaseManager.switchDatabase(dbPath);
        }
    }

    property string existingProjectName

    StyledDialog {
        id: existingProjectDialog
        anchors.centerIn: parent
        width: 420
        title: qsTr("Project Already Exists")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Switch")
        onAccepted: {
            // The dialog is closed by now; switch once it has restored focus
            // (see onRequestSwitchProject for the crash).
            let name = existingProjectName;
            Qt.callLater(function () {
                let ok = configManager.switchToProject(name);
                if (ok) {
                    let dbPath = configManager.projectDbPath(name);
                    databaseManager.switchDatabase(dbPath);
                }
            });
        }
        Label {
            width: existingProjectDialog.availableWidth
            text: qsTr("Project \"%1\" already exists. Switch to it?").arg(existingProjectName)
            wrapMode: Text.WordWrap
        }
    }

    // Clean project confirmation dialog
    StyledDialog {
        id: cleanProjectDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Clean Project")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: cleanProjectDialog.availableWidth
            text: qsTr("Clean project \"%1\"? All files and data will be permanently removed.").arg(activeProject)
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
    StyledDialog {
        id: deleteConfirmDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Delete Projects")
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            spacing: 5
            Label {
                Layout.fillWidth: true
                text: qsTr("Delete the selected project(s)? This cannot be undone.")
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
            // Closes the active DB before deleting its files, then opens the
            // new active project's DB (chosen by ConfigManager).
            let err = databaseManager.deleteProjects(root.selectedProjects);
            if (err !== "")
                messageBox.showError(qsTr("Project Deletion Failed"), err);
            root.selectedProjects = [];
        }
        onRejected: {
            root.selectedProjects = [];
        }
    }

    // Delete model confirmation dialog
    StyledDialog {
        id: deleteModelDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Delete Models")
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            spacing: 5
            Label {
                Layout.fillWidth: true
                text: qsTr("Delete the selected model(s)? This cannot be undone.")
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.selectedModels
                delegate: Label {
                    required property var modelData
                    text: "  • " + modelData
                    font.weight: Font.Bold
                }
            }
        }
        onAccepted: {
            helpers.deleteLocalModels(root.selectedModels);
            root.selectedModels = [];
        }
        onRejected: {
            root.selectedModels = [];
        }
    }

    // OpenAlex API key dialog
    StyledDialog {
        id: openAlexKeyDialog
        anchors.centerIn: parent
        width: 420
        title: qsTr("OpenAlex API Key")
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: qsTr("Enter your OpenAlex API key:")
                Layout.fillWidth: true
            }
            StyledTextField {
                id: openAlexKeyField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste your API key...")
                text: configManager.openAlexApiKey
                focus: true
                onAccepted: openAlexKeyDialog.accept()
            }
        }
        onAccepted: {
            configManager.openAlexApiKey = openAlexKeyField.text.trim();
        }
    }

    // User-facing information and errors for the main window.
    MessageBox {
        id: messageBox
    }

    // Ingestion error: papers that could not be indexed (and were removed
    // again so they can be re-added).
    Connections {
        target: engineController
        function onIngestionError(message) {
            messageBox.showError(qsTr("Indexing Failed"), message);
        }
    }

    // Backend setup modal
    BackendWindow {
        id: backendDialog
    }

    // Dialog for adding a local GGUF model (+ optional mmproj)
    StyledDialog {
        id: localModelDialog
        anchors.centerIn: parent
        title: qsTr("Add Local Model")
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 520
        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            // ── Model GGUF ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: qsTr("Model:")
                    Layout.minimumWidth: 60
                }
                StyledTextField {
                    id: modelPathField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Select a .gguf model file...")
                    readOnly: true
                }
                StyledButton {
                    text: qsTr("Browse...")
                    onClicked: localModelFileDlg.open()
                }
            }

            // ── MMProj GGUF ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: qsTr("MMProj:")
                    Layout.minimumWidth: 60
                }
                StyledTextField {
                    id: mmprojPathField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Select a .gguf mmproj file...")
                    readOnly: true
                }
                StyledButton {
                    text: qsTr("Browse...")
                    onClicked: localMmprojFileDlg.open()
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
        onAccepted: {
            let modelPath = modelPathField.text.trim();
            let mmprojPath = mmprojPathField.text.trim();
            if (modelPath === "") {
                return;
            }
            helpers.addLocalSummarizer(modelPath, mmprojPath);
        }
        onClosed: {
            modelPathField.clear();
            mmprojPathField.clear();
        }
    }

    // Backing file dialog — model GGUF
    FileDialog {
        id: localModelFileDlg
        title: qsTr("Select GGUF Model File")
        fileMode: FileDialog.OpenFile
        nameFilters: ["GGUF files (*.gguf)", "All files (*)"]
        onAccepted: {
            modelPathField.text = selectedFile.toString();
        }
    }

    // Backing file dialog — mmproj GGUF
    FileDialog {
        id: localMmprojFileDlg
        title: qsTr("Select MMProj GGUF File")
        fileMode: FileDialog.OpenFile
        nameFilters: ["GGUF files (*.gguf)", "All files (*)"]
        onAccepted: {
            mmprojPathField.text = selectedFile.toString();
        }
    }

    // Model copy progress in footer
    Connections {
        target: helpers
        function onModelCopyProgress(percentage, fileName) {
            footerLabel.text = qsTr("Copying model: %1 — %2%").arg(fileName).arg(percentage);
        }
        function onModelCopyFinished(success, destPath, error) {
            if (success) {
                footerLabel.text = qsTr("Model copied: %1").arg(destPath);
            } else {
                footerLabel.text = qsTr("Copy failed: %1").arg(error);
            }
        }
    }

    // Status bar
    footer: StatusBar {
        id: footerLabel
        text: qsTr("%1 -- Ready").arg(activeProject)
        Component.onCompleted: text = qsTr("%1 -- Ready").arg(activeProject)
    }
}
