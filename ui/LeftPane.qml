pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: leftPane
    property var activeTheme: ({})
    color: Colors.surface0
    border.width: 1
    radius: 5
    opacity: 1
    signal openRagWindow

    // File dialog for selecting PDFs
    FileDialog {
        id: pdfFileDialog
        title: "Select PDF files"
        nameFilters: ["PDF files (*.pdf)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            ingestionEngine.ingestFiles(selectedFiles);
            ingestionProgress.visible = true;
            addfilebtn.enabled = false;
            addfolderbtn.enabled = false;
        }
    }

    // Folder dialog for selecting a directory
    FolderDialog {
        id: pdfFolderDialog
        title: "Select a folder containing PDFs"
        onAccepted: {
            ingestionEngine.ingestFolder(selectedFolder.toString());
            ingestionProgress.visible = true;
            addfilebtn.enabled = false;
            addfolderbtn.enabled = false;
        }
    }

    // Bind ingestion signals
    Connections {
        target: ingestionEngine
        function onProgress(value) {
            ingestionProgress.value = value / 100;
        }
        function onStatus(text) {
            statusLabel.text = text;
        }
        function onStatus2(text) {
            massStatusLabel.text = text;
        }
        function onFinished() {
            ingestionProgress.visible = false;
            ingestionProgress.value = 0;
            addfilebtn.enabled = true;
            addfolderbtn.enabled = true;
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
            ProgressBar {
                id: ingestionProgress
                value: 0
                visible: false
                Layout.fillWidth: true
                Layout.minimumHeight: 10
            }
            Label {
                id: statusLabel
                text: ""
                visible: ingestionProgress.visible
                elide: Text.ElideMiddle
            }
            Label {
                id: massStatusLabel
                text: ""
                visible: ingestionProgress.visible
                wrapMode: Text.WordWrap
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
