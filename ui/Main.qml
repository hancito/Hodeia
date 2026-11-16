pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ThemedWindow {
    id: root
    preferredWidth: 1920
    preferredHeight: 1080

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

    onClosing: function (close) {
        // A model copy runs on a pool thread that still references `helpers`:
        // quitting now would destroy it under the copy.  The copy window
        // tells the user to wait.
        if (modelCopyDialog.opened) {
            close.accepted = false;
            return;
        }
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
        onRequestClearProject: clearProjectDialog.open()
        onRequestDeleteModels: deleteModelDialog.open()
        onRequestOpenAlexApiKey: openAlexKeyDialog.open()
        onRequestAbout: aboutDialog.open()
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
                // Proportional width: fixed pixel minimums (600 + 1000) overflowed
                // the window with display scaling (200 % → ~960 logical px).
                SplitView.preferredWidth: mainSplitView.width * 0.35
                SplitView.minimumWidth: 260
                statusBarLabel: footerLabel
            }

            DisplayPane {
                id: displayPaneItem
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
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

    // Clear project data confirmation dialog
    StyledDialog {
        id: clearProjectDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Clear Project Data")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: clearProjectDialog.availableWidth
            text: qsTr("Clear all data of project \"%1\"? Its papers, summaries and queries will be permanently removed; the empty project is kept.").arg(activeProject)
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

    // About dialog: app identity (from the README), license, and the
    // third-party components with their licenses (keep in sync with NOTICE).
    StyledDialog {
        id: aboutDialog
        anchors.centerIn: parent
        width: 520
        title: qsTr("About Hodeia")
        standardButtons: Dialog.Ok

        readonly property var components: [
            { name: "Qt 6", url: "https://www.qt.io", license: "LGPL-3.0" },
            { name: "llama.cpp / ggml", url: "https://github.com/ggml-org/llama.cpp", license: "MIT" },
            { name: "MuPDF", url: "https://mupdf.com", license: "AGPL-3.0" },
            { name: "Tesseract OCR", url: "https://github.com/tesseract-ocr/tesseract", license: "Apache-2.0" },
            { name: "Leptonica", url: "https://github.com/DanBloomberg/leptonica", license: "BSD-2-Clause" },
            { name: "SQLite", url: "https://sqlite.org", license: qsTr("Public domain") },
            { name: "sqlite-vec", url: "https://github.com/asg017/sqlite-vec", license: "MIT / Apache-2.0" },
            { name: "Vulkan Loader", url: "https://github.com/KhronosGroup/Vulkan-Loader", license: "Apache-2.0" },
            { name: "Google Sans, Google Sans Code…", url: "https://fonts.google.com", license: "SIL OFL 1.1" }
        ]

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Image {
                    Layout.alignment: Qt.AlignTop
                    source: "qrc:/Resources/hodeia.png"
                    sourceSize.width: 64
                    sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: Qt.application.displayName
                        font.pixelSize: Style.fontTitle
                        font.weight: Font.Bold
                    }
                    Label {
                        text: qsTr("Version %1").arg(Qt.application.version)
                        color: Colors.overlay1
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Your personal research assistant — powered by AI.")
                        font.italic: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Manage, summarize and search your collection of academic papers: import PDFs, get structured AI summaries, pull their references from OpenAlex, and ask questions across your entire library.")
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("“Hodeia” — from the Ancient Greek ὁδός (hodós), meaning “path”.")
                color: Colors.overlay1
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Free and open source software, licensed under the <a href=\"%1\">GNU AGPL-3.0</a>.<br>Source code: <a href=\"%2\">%2</a>")
                      .arg("https://www.gnu.org/licenses/agpl-3.0.html")
                      .arg("https://github.com/hancito/Hodeia")
                textFormat: Text.StyledText
                linkColor: Colors.primary
                wrapMode: Text.WordWrap
                onLinkActivated: link => Qt.openUrlExternally(link)
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Colors.surface1
            }

            Label {
                text: qsTr("Third-Party Components")
                font.weight: Font.Bold
            }

            // Two Repeaters (name / license) pinned to explicit rows, so the
            // license column lines up.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 24
                rowSpacing: 4
                Repeater {
                    model: aboutDialog.components
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: "<a href=\"%1\">%2</a>".arg(modelData.url).arg(modelData.name)
                        textFormat: Text.StyledText
                        linkColor: Colors.primary
                        onLinkActivated: link => Qt.openUrlExternally(link)
                    }
                }
                Repeater {
                    model: aboutDialog.components
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        Layout.fillWidth: true
                        text: modelData.license
                        color: Colors.overlay1
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Each component remains under its own license; see the NOTICE file shipped with Hodeia for the full notices.")
                font.pixelSize: Style.fontSmall
                color: Colors.overlay1
                wrapMode: Text.WordWrap
            }
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
            // Open first: addLocalSummarizer() may report an invalid path
            // synchronously, which closes the window again.
            modelCopyDialog.start();
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

    // Blocking window shown while a local model is copied into the models
    // folder: modal, no buttons, cannot be dismissed (Escape / click outside)
    // until helpers reports the end of the copy.
    StyledDialog {
        id: modelCopyDialog
        property int percentage: 0
        property string fileName: ""

        function start() {
            percentage = 0;
            fileName = "";
            open();
        }

        anchors.centerIn: parent
        width: 460
        title: qsTr("Adding Model")
        closePolicy: Popup.NoAutoClose
        // Take keyboard focus away from the window behind.
        focus: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: modelCopyDialog.fileName === "" ? qsTr("Preparing the copy...") : qsTr("Copying %1").arg(modelCopyDialog.fileName)
                elide: Text.ElideMiddle
            }
            StyledProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: modelCopyDialog.percentage
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("%1% — please wait, large models can take a few minutes.").arg(modelCopyDialog.percentage)
                wrapMode: Text.WordWrap
                color: Colors.overlay2
                font.pixelSize: Style.fontSmall
            }
        }
    }

    Connections {
        target: helpers
        function onModelCopyProgress(percentage, fileName) {
            modelCopyDialog.percentage = percentage;
            modelCopyDialog.fileName = fileName;
            footerLabel.text = qsTr("Copying model: %1 — %2%").arg(fileName).arg(percentage);
        }
        function onModelCopyFinished(success, destPath, error) {
            modelCopyDialog.close();
            if (success) {
                footerLabel.text = qsTr("Model copied: %1").arg(destPath);
                messageBox.showInfo(qsTr("Model Added"), qsTr("The model was added and can now be selected in the toolbar.\n\n%1").arg(destPath));
            } else {
                footerLabel.text = qsTr("Copy failed: %1").arg(error);
                messageBox.showError(qsTr("Model Copy Failed"), qsTr("The model could not be added:\n%1").arg(error));
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
