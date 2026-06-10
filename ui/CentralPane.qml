import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Pdf

Rectangle {
    id: centralPane
    color: "transparent"
    opacity: 0.8
    border.color: "black"
    border.width: 1
    radius: 10

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: 5
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
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: textTab
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        TextArea {
                            id: textEditor
                            readOnly: true
                            textFormat: TextEdit.MarkdownText
                            wrapMode: TextEdit.WordWrap

                            text: "# TextArea Markdown\n\n" + "1. This is a scrollable area.\n" + "2. You can select this text.\n" + "3. **Bold** and *italics* work perfectly"
                        }
                    }

                    RowLayout {
                        id: buttonRow
                        spacing: 10
                        Layout.fillWidth: true

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            text: qsTr("Save")
                            icon.width: 24
                            icon.height: 24
                            icon.source: "qrc:/Resources/save.svg"
                            onClicked: console.log("Save Text")
                        }

                        Button {
                            text: qsTr("Edit")
                            icon.width: 24
                            icon.height: 24
                            icon.source: "qrc:/Resources/edit.svg"
                            onClicked: textEditor.readOnly = !textEditor.readOnly
                        }
                    }
                }
            }

            Item {
                id: latexTab
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        TextArea {
                            id: latexEditor
                            readOnly: true
                            wrapMode: TextEdit.WordWrap
                            placeholderText: "Latex will be here"
                        }
                    }

                    RowLayout {
                        id: buttonRow2
                        spacing: 10
                        Layout.fillWidth: true

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            text: qsTr("Save")
                            icon.width: 24
                            icon.height: 24
                            icon.source: "qrc:/Resources/save.svg"
                            onClicked: console.log("Save Text")
                        }

                        Button {
                            text: qsTr("Edit")
                            icon.width: 24
                            icon.height: 24
                            icon.source: "qrc:/Resources/edit.svg"
                            onClicked: textEditor.readOnly = !textEditor.readOnly
                        }
                    }
                }
            }

            Item {
                id: pdfTab
                PdfScrollablePageView {
                    anchors.fill: parent
                    document: PdfDocument {
                        source: "file:///home/hancitog/Developpement/Paperize_CPP/attention.pdf"
                    }
                }
            }
        }
    }
}
