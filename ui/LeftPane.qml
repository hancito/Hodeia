pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: leftPane
    property int elapsedTime: 0
    color: Colors.surface0
    border.width: 1
    radius: 5
    opacity: 1
    signal openRagWindow
    property var statusBarLabel: null

    // React to engine finishing: re-enable buttons, hide progress
    Connections {
        target: engineController
        function onIngestionFinished() {
            spinner.visible = false;
            spinner.running = false;
            executionTimer.stop();
            addfilebtn.enabled = true;
            addfolderbtn.enabled = true;
            if (leftPane.statusBarLabel) {
                leftPane.statusBarLabel.text = "Ingestion finished in " + (leftPane.elapsedTime / 10) + " seconds!";
            }
        }

        function onIngestionStarted(statusLog) {
            leftPane.elapsedTime = 0;
            statusLabel.visible = true;
            statusLabel.text = statusLog;
        }
    }

    Timer {
        id: executionTimer
        interval: 100 // Updates every 100 milliseconds (0.1 seconds)
        running: false
        repeat: true

        onTriggered: {
            leftPane.elapsedTime += 1;
        }
    }

    // File dialog for selecting PDFs
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
            console.log("Img path:", imgPath);
            console.log("DB path:", dbPath);
            engineController.startEngine(selectedFiles, imgPath, dbPath);
        }
        onRejected: {
            console.log("Canceled");
        }
    }

    // Folder dialog for selecting a directory
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 15

        // Add buttons section
        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Button {
                id: addfilebtn
                ToolTip.text: "Add a file"
                ToolTip.visible: hovered
                display: AbstractButton.IconOnly
                padding: 0
                icon.width: 40
                icon.height: 40
                icon.source: "qrc:/Resources/arrow-right-to-file.svg"
                Layout.fillWidth: true
                onClicked: pdfFileDialog.open()
            }

            Button {
                id: addfolderbtn
                ToolTip.text: "Add a folder"
                ToolTip.visible: hovered
                padding: 0
                icon.width: 40
                icon.height: 40
                icon.source: "qrc:/Resources/folder-open.svg"
                Layout.fillWidth: true
                onClicked: pdfFolderDialog.open()
            }
        }

        // Progress section
        ColumnLayout {
            spacing: 10
            Layout.fillWidth: true
            BusyIndicator {
                id: spinner
                running: false
                visible: false
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            }
            Label {
                id: statusLabel
                visible: spinner.visible
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
                text: "Elapsed Time: " + (leftPane.elapsedTime / 10).toFixed(1) + "s"
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            }
        }

        // Content area - takes available space
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
        }

        // Query section
        ColumnLayout {
            spacing: 10
            Layout.fillWidth: true
            Button {
                text: qsTr("Query your library")
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                onClicked: leftPane.openRagWindow()
            }

            Label {
                text: "Context Window"
                Layout.fillWidth: true
            }

            SpinBox {
                id: contextWindowSpinner
                from: 1000
                to: 256000
                value: 4000
                stepSize: 1000
                Layout.fillWidth: true
            }

            Label {
                text: "Model"
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            ComboBox {
                id: modelComboBox
                Layout.fillWidth: true
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
        }
    }
}
