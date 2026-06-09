import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: leftPane
    border.color: "black"
    border.width: 1
    radius: 10
    color: "#f5f5f5"
    opacity: 1.0

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 20
            
            // Add buttons section
            Row {
                spacing: 10
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                Button {
                    ToolTip.text: "Add a file"
                    icon.width: 40
                    icon.height: 40
                    icon.source: "qrc:/Resources/arrow-right-to-file.svg"
                    onClicked: console.log("Add Paper")
                }

                Button {
                    ToolTip.text: "Add a folder"
                    icon.width: 40
                    icon.height: 40
                    icon.source: "qrc:/Resources/folder-open.svg"
                    onClicked: console.log("Add Folder")
                }
            }

            // Query section
            Column {
                width: parent.width
                spacing: 10

                Button {
                    text: qsTr("Query your library")
                    width: parent.width
                    onClicked: console.log("Query Library")
                }

                Label {
                    text: "Context Window"
                }

                SpinBox {
                    id: contextWindowSpinner
                    from: 1000
                    to: 256000
                    value: 4000
                    stepSize: 1000
                    width: parent.width
                }

                Label {
                    text: "Model"
                    horizontalAlignment: Text.AlignHCenter
                }

                ComboBox {
                    id: modelComboBox
                    width: parent.width
                    // Model options would be populated here
                }
            }

           
    }
}

