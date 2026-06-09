import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: centralPane
    color: "transparent"
    opacity: 0.8
    border.color: "black"
    border.width: 1
    radius: 10

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 15

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton {
                text: qsTr("Text")
            }
            TabButton {
                text: qsTr("Latex")
            }
            TabButton {
                text: qsTr("PDF")
            }
        }

        StackLayout {
            currentIndex: tabBar.currentIndex
            Item {
                id: textTab
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10
                    TextArea {
                        id: textEditor
                        readOnly: true
                        wrapMode: Text.WordWrap
                        Layout.minimumHeight: 800

                        Component.onCompleted: {
                            text = "Text content will appear here...";
                        }
                    }
                    RowLayout {
                        id: buttonRow
                        spacing: 10
                        anchors.top: textEditor.bottom

                        Button {
                            text: qsTr("Save")
                            icon.width: 40
                            icon.height: 40
                            icon.source: "qrc:/Resources/save.svg"
                            onClicked: console.log("Save Text")
                        }

                        Button {
                            text: qsTr("Edit")
                            icon.width: 40
                            icon.height: 40
                            icon.source: "qrc:/Resources/edit.svg"
                            onClicked: textEditor.readOnly = !textEditor.readOnly
                        }
                    }
                }
            }
            Item {
                id: latexTab
            }
            Item {
                id: pdfTab
            }
        }

        // Main content area - fills available space
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
        }
    }
}
