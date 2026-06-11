pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: dbPane
    property var activeTheme: ({})
    border.color: activeTheme.overlay0 || "#626573"
    border.width: 1
    radius: 10
    color: activeTheme.surface0 || "#313442"
    opacity: 1.0

    // gradient: Colors.invertedGreyBorder

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Query History")
                color: dbPane.activeTheme.text || "#f8f8f2"
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
