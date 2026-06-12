pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: leftPane
    property var activeTheme: ({})
    border.color: activeTheme.overlay0 || "#626573"
    border.width: 1
    radius: 10
    color: activeTheme.surface0 || "#313442"
    opacity: 1.0
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
                color: leftPane.activeTheme.text || "#f8f8f2"
            }
            Label {
                text: "Mass Ingestion Status"
                wrapMode: Text.WordWrap
                visible: ingestionProgress.visible
                color: leftPane.activeTheme.text || "#f8f8f2"
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
                color: leftPane.activeTheme.text || "#f8f8f2"
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
                color: leftPane.activeTheme.text || "#f8f8f2"
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
