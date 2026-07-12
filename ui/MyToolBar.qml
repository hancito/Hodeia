pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ToolBar {
    id: toolBar

    // ── Properties migrated from LeftPane ──
    property int elapsedTime: 0
    signal openRagWindow
    property var statusBarLabel: null
    property bool busy: false

    // Load models for the active backend when this component is ready.
    Component.onCompleted: modelHelper.loadActiveBackendModels()

    // ── File dialog for selecting PDFs ──
    FileDialog {
        id: pdfFileDialog
        title: "Select PDF files"
        nameFilters: ["PDF files (*.pdf)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            spinner.visible = true;
            spinner.running = true;
            executionTimer.running = true;
            executionTimer.start();
            addfilebtn.enabled = false;
            addfolderbtn.enabled = false;
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
        title: "Select a folder containing PDFs"
        onAccepted: {
            spinner.visible = true;
            spinner.running = true;
            executionTimer.running = true;
            executionTimer.start();
            addfilebtn.enabled = false;
            addfolderbtn.enabled = false;
            var imgPath = configManager.projectImgPath(configManager.lastProject);
            var dbPath = configManager.projectDbPath(configManager.lastProject);
            engineController.startEngine(engineController.findPdfFiles(selectedFolder), imgPath, dbPath);
        }
    }

    // ── React to engine finishing: re-enable buttons, hide progress ──
    Connections {
        target: engineController
        function onIngestionFinished() {
            spinner.visible = false;
            spinner.running = false;
            executionTimer.stop();
            toolBar.busy = false;
            if (toolBar.statusBarLabel) {
                toolBar.statusBarLabel.text = statusLabel.text;
            }
            statusLabel.visible = false;
        }

        function onIngestionStarted() {
            toolBar.elapsedTime = 0;
            statusLabel.visible = true;
            toolBar.busy = true;
        }

        function onIngestionStatus(statusLog) {
            statusLabel.text = statusLog;
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

    // ── React to modelHelper modelsChecked to populate the model combobox ──
    Connections {
        target: modelHelper
        function onModelsChecked(success, models, error) {
            // Read activeModel BEFORE assigning the model to avoid the
            // ComboBox resetting to index 0 and overwriting it via
            // onCurrentTextChanged.
            var savedModel = configManager.activeModel;
            if (success && models.length > 0) {
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
    }

    // ── Elapsed-time timer ──
    Timer {
        id: executionTimer
        interval: 100 // Updates every 100 milliseconds (0.1 seconds)
        running: false
        repeat: true

        onTriggered: {
            toolBar.elapsedTime += 1;
        }
    }

    // ── ToolBar content ──
    RowLayout {
        spacing: 10
        Layout.fillWidth: true
        // Add file button
        Button {
            id: addfilebtn
            enabled: !toolBar.busy
            ToolTip.text: "Add a file"
            ToolTip.visible: hovered
            display: AbstractButton.IconOnly
            padding: 0
            icon.width: 40
            icon.height: 40
            icon.source: "qrc:/Resources/arrow-right-to-file.svg"
            onClicked: pdfFileDialog.open()
        }

        // Add folder button
        Button {
            id: addfolderbtn
            enabled: !toolBar.busy
            ToolTip.text: "Add a folder"
            ToolTip.visible: hovered
            padding: 0
            icon.width: 40
            icon.height: 40
            icon.source: "qrc:/Resources/folder-open.svg"
            onClicked: pdfFolderDialog.open()
        }

        ToolSeparator {}

        // ── Progress section (inline in toolbar) ──
        BusyIndicator {
            id: spinner
            running: false
            visible: false
        }

        Label {
            id: statusLabel
            visible: false
            Layout.maximumWidth: 300
            elide: Text.ElideMiddle
        }

        Label {
            id: status2
            visible: spinner.visible
            text: "Ingesting..."
        }

        Text {
            id: timerIndicator
            font.family: Fonts.mono
            visible: spinner.visible
            text: {
                var totalSec = Math.floor(toolBar.elapsedTime / 10);
                if (totalSec >= 3600) {
                    var h = Math.floor(totalSec / 3600);
                    var m = Math.floor((totalSec % 3600) / 60);
                    return "Elapsed Time: " + h + "h " + m.toString().padStart(2, "0") + "m";
                } else if (totalSec >= 60) {
                    var m = Math.floor(totalSec / 60);
                    var s = totalSec % 60;
                    return "Elapsed Time: " + m + "m " + s.toString().padStart(2, "0") + "s";
                } else {
                    return "Elapsed Time: " + totalSec + "s";
                }
            }
        }

        ToolSeparator {}

        Label {
            text: "Active Model"
        }

        ComboBox {
            id: modelComboBox
            Layout.preferredWidth: 300

            background: Rectangle {
                palette.text: Colors.text
                color: Colors.surface1
                border.width: 2
            }
            onCurrentTextChanged: {
                if (currentText !== "") {
                    configManager.activeModel = currentText;
                }
            }
        }
        CheckBox {
            id: visualCheckBox
            text: "Use visual context"
            checked: configManager.useVisualContext
            onCheckedChanged: configManager.useVisualContext = checked
        }

        ToolSeparator {}
        Item {
            Layout.fillWidth: true
        }
        // ── Query section ──
        ToolButton {
            text: qsTr("Query your library")
            Layout.minimumHeight: 40
            icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
            onClicked: toolBar.openRagWindow()
        }
    }
}
