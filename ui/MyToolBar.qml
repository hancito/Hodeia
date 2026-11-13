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
    property int elapsedTime: 0
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
            spinner.visible = true;
            spinner.running = true;
            executionTimer.running = true;
            executionTimer.start();
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
            spinner.visible = true;
            spinner.running = true;
            executionTimer.running = true;
            executionTimer.start();
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

    // ── React to helpers modelsChecked to populate the model combobox ──
    Connections {
        target: helpers
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

        // ── Progress section (inline in toolbar) ──
        StyledBusyIndicator {
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
            text: qsTr("Ingesting...")
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
                    return qsTr("Elapsed Time: %1h %2m").arg(h).arg(m.toString().padStart(2, "0"));
                } else if (totalSec >= 60) {
                    var m = Math.floor(totalSec / 60);
                    var s = totalSec % 60;
                    return qsTr("Elapsed Time: %1m %2s").arg(m).arg(s.toString().padStart(2, "0"));
                } else {
                    return qsTr("Elapsed Time: %1s").arg(totalSec);
                }
            }
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
