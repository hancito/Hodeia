import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ToolBar {
    id: toolBar
    leftPadding: Style.paneMargin
    rightPadding: Style.paneMargin
    topPadding: 4
    bottomPadding: 4

    background: Rectangle {
        color: Colors.mantle
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Colors.surface0
        }
    }

    // ── Properties migrated from LeftPane ──
    signal openRagWindow
    property var statusBarLabel: null
    property bool busy: false

    // Load models for the active backend when this component is ready.
    Component.onCompleted: helpers.loadActiveBackendModels()

    // ── File dialog for selecting PDFs ──
    FileDialog {
        id: pdfFileDialog
        title: qsTr("Select PDF files")
        nameFilters: ["PDF files (*.pdf)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            toolBar.busy = true;
            var imgPath = configManager.projectImgPath(configManager.lastProject);
            var dbPath = configManager.projectDbPath(configManager.lastProject);
            engineController.startEngine(selectedFiles, imgPath, dbPath);
        }
        onRejected: {
            console.log("Canceled");
        }
    }

    // ── Folder dialog for selecting a directory ──
    FolderDialog {
        id: pdfFolderDialog
        title: qsTr("Select a folder containing PDFs")
        onAccepted: {
            toolBar.busy = true;
            var imgPath = configManager.projectImgPath(configManager.lastProject);
            var dbPath = configManager.projectDbPath(configManager.lastProject);
            engineController.startEngine(engineController.findPdfFiles(selectedFolder), imgPath, dbPath);
        }
    }

    // ── React to engine finishing: re-enable buttons, hide progress ──
    Connections {
        target: engineController
        function onIngestionFinished() {
            toolBar.busy = false;
        }

        function onIngestionStarted() {
            toolBar.busy = true;
        }

        // The progress itself is shown by IngestionDialog; the status bar
        // keeps the latest step so it remains visible after the import.
        function onIngestionStatus(statusLog) {
            if (toolBar.statusBarLabel)
                toolBar.statusBarLabel.text = statusLog;
        }
    }

    // ── React to summarizer state ──
    Connections {
        target: summaryController
        function onSummaryStarted() {
            toolBar.busy = true;
        }
        function onSummaryFinished() {
            toolBar.busy = false;
        }
        function onSummaryStopped() {
            toolBar.busy = false;
        }
    }

    // ── React to helpers modelsChecked to populate the model combobox ──
    Connections {
        target: helpers
        function onModelsChecked(success, models, error) {
            if (!success)
                return;
            if (models.length === 0) {
                // e.g. the last local model was deleted
                modelComboBox.model = [];
                configManager.activeModel = "";
                return;
            }
            // Read activeModel BEFORE assigning the model to avoid the
            // ComboBox resetting to index 0 and overwriting it via
            // onCurrentTextChanged.
            var savedModel = configManager.activeModel;
            var idx = models.indexOf(savedModel);
            if (idx < 0) {
                idx = 0;
                savedModel = models[0];
            }
            modelComboBox.model = models;
            modelComboBox.currentIndex = idx;
            // Restore after ComboBox has swallowed the reset
            configManager.activeModel = savedModel;
        }
    }

    // ── ToolBar content ──
    RowLayout {
        spacing: 10
        Layout.fillWidth: true
        // Add file button
        StyledButton {
            id: addfilebtn
            flat: true
            enabled: !toolBar.busy
            toolTip: qsTr("Add a file")
            display: AbstractButton.IconOnly
            padding: 4
            icon.width: 32
            icon.height: 32
            icon.source: "qrc:/Resources/arrow-right-to-file.svg"
            onClicked: pdfFileDialog.open()
        }

        // Add folder button
        StyledButton {
            id: addfolderbtn
            flat: true
            enabled: !toolBar.busy
            toolTip: qsTr("Add a folder")
            display: AbstractButton.IconOnly
            padding: 4
            icon.width: 32
            icon.height: 32
            icon.source: "qrc:/Resources/folder-open.svg"
            onClicked: pdfFolderDialog.open()
        }

        ToolSeparator {}

        Label {
            text: qsTr("Active Model")
        }

        StyledComboBox {
            id: modelComboBox
            Layout.preferredWidth: 300
            onCurrentTextChanged: {
                if (currentText !== "") {
                    configManager.activeModel = currentText;
                }
            }
        }
        StyledComboBox {
            id: visualModeCombo
            Layout.preferredWidth: 220
            Component.onCompleted: {
                var saved = configManager.visualMode;
                model = configManager.visualModeList();
                currentIndex = model.indexOf(saved);
                configManager.visualMode = saved;
            }
            onCurrentTextChanged: {
                if (currentText !== "") {
                    configManager.visualMode = currentText;
                }
            }
        }

        ToolSeparator {}
        Item {
            Layout.fillWidth: true
        }
        // ── Query section ──
        StyledButton {
            flat: true
            text: qsTr("Query your library")
            Layout.minimumHeight: 40
            icon.width: 24
            icon.height: 24
            icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
            onClicked: toolBar.openRagWindow()
        }
    }
}
