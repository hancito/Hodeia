import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: rightPane
    border.color: "black"
    border.width: 5
    radius: 10
    opacity: 0.5

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: 5
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 15
        spacing: 5

        TextField {
            id: paperSearch
            Layout.fillWidth: true
            placeholderText: "Search paper"
            //clearButtonEnabled: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Button {
                ToolTip.text: "Summarize"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: console.log("Summarize")
            }

            Button {
                ToolTip.text: "Refresh"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/refresh.svg"
                onClicked: console.log("Refresh")
            }

            Button {
                ToolTip.text: "Generate Report from selection"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/list.svg"
                onClicked: console.log("Generate Report")
            }

            Button {
                ToolTip.text: "Delete"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: console.log("Delete Paper")
            }
        }

        // Progress section
        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true

            ProgressBar {
                id: summarizeProgress
                value: 24
                Layout.fillWidth: true
                visible: false
            }

            Label {
                text: "Summarize"
                Layout.fillWidth: true
                visible: summarizeProgress.visible
            }

            Label {
                text: "Elapsed Time"
                Layout.fillWidth: true
                visible: summarizeProgress.visible
            }
        }

        // Controls
        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            CheckBox {
                text: "Select All"
            }
            Item {
                Layout.fillWidth: true
            }

            Label {
                text: "Paper Number:"
            }

            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            Label {
                text: "Sort by"
            }

            ComboBox {
                model: ["Title", "Date", "Pages", "Model"]
                Layout.fillWidth: true
            }

            CheckBox {
                text: "Descending"
            }
        }

        // Paper List
        ListView {
            id: paperListView
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Add bounds behavior for responsiveness
            boundsBehavior: Flickable.DragAndOvershootBounds

            // Ensure it takes remaining space
            Layout.minimumHeight: parent.height * 0.3
        }
    }
}
