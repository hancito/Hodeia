import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: leftPane
    border.color: "black"
    border.width: 1
    radius: 10
    color: "#FFFFFF"
    opacity: 0.5
    signal openRagWindow

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 15
        // Add buttons section
        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Button {
                ToolTip.text: "Add a file"
                icon.width: 40
                icon.height: 40
                icon.source: "qrc:/Resources/arrow-right-to-file.svg"
                Layout.fillWidth: true
                Layout.minimumHeight: 50
                onClicked: console.log("Add Paper")
            }

            Button {
                ToolTip.text: "Add a folder"
                icon.width: 40
                icon.height: 40
                icon.source: "qrc:/Resources/folder-open.svg"
                Layout.fillWidth: true
                Layout.minimumHeight: 50
                onClicked: console.log("Add Folder")
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
                text: "Ingesting ..."
                visible: ingestionProgress.visible
            }
            Label {
                text: "Mass Ingestion Status"
                wrapMode: Text.WordWrap
                visible: ingestionProgress.visible
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
                // Model options would be populated here
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
        }
    }
}
