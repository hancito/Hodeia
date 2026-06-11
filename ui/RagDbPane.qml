import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: dbPane
    border.color: "black"
    border.width: 1
    radius: 10
    color: "#FFFFFF"
    opacity: 0.5
    // gradient: Colors.invertedGreyBorder

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Query History")
                Layout.fillWidth: true
                font.pointSize: 16
                font.weight: Font.Bold
                // Align the text inside the 300x100 box
                horizontalAlignment: Text.AlignHCenter // Options: AlignLeft, AlignRight, AlignHCenter, AlignJustify
                verticalAlignment: Text.AlignVCenter   // Options: AlignTop, AlignBottom, AlignVCenter
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true
            CheckBox {
                text: "Select All"
            }
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

        ColumnLayout {
            spacing: 5
            ListView {
                id: queryView
                Layout.fillWidth: true
                Layout.fillHeight: true
                

                // Add bounds behavior for responsiveness
                boundsBehavior: Flickable.DragAndOvershootBounds

                // Ensure it takes remaining space
                Layout.minimumHeight: parent.height * 0.3
            }

            RowLayout {
                id: buttonRowdbquery
                spacing: 10
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Delete")
                    icon.width: 24
                    icon.height: 24
                    icon.source: "qrc:/Resources/trash-can.svg"
                    onClicked: console.log("Delete")
                }
            }
        }
    }
}
