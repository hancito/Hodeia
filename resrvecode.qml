
LeftPane {
    id: leftPaneItem
    anchors.top: parent.top
    anchors.topMargin: 10
    anchors.left: parent.left
    anchors.leftMargin: 10
    width: parent.width * 0.10
    height: parent.height - 10
}

CentralPane {
    id: centralPaneItem
    anchors.top: parent.top
    anchors.topMargin: 10
    anchors.left: leftPaneItem.right
    anchors.leftMargin: 10
    width: parent.width * 0.50
    height: parent.height - 10
}

RightPane {
    id: rightPaneItem
    anchors.top: parent.top
    anchors.topMargin: 10
    anchors.left: centralPaneItem.right
    anchors.leftMargin: 10
    anchors.right: parent.right
    anchors.rightMargin: 10
    width: parent.width * 0.40
    height: parent.height - 10
}





Rectangle {
    id: leftPane
    border.color: "black"
    border.width: 5
    radius: 100
    color: "#f5f5f5"
    anchors.rightMargin: -radius
    opacity: 0.5

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 20

            // Add buttons section
            Row {
                spacing: 10
                width: parent.width
                Button {
                    text: qsTr("Add Paper")
                    ToolTip.text: "Add a file"
                    // icon.source: "qrc:/Ressources/arrow-right-to-file.svg"
                    icon.width: 40
                    icon.height: 40
                    onClicked: console.log("Add Paper")
                }

                Button {
                    text: qsTr("Add Folder")
                    ToolTip.text: "Add a folder"
                    // icon.source: "qrc:/Ressources/folder-open.svg"
                    icon.width: 40
                    icon.height: 40
                    onClicked: console.log("Add Folder")
                }
            }

            // Progress section
            Column {
                width: parent.width
                spacing: 5

                ProgressBar {
                    id: ingestionProgress
                    value: 0
                    visible: true
                    width: parent.width
                }
                Label {
                    text: "Ingesting ..."
                    visible: ingestionProgress.visible
                }
                Label {
                    text: "Mass Ingestion Status"
                    wrapMode: Text.WordWrap
                }
            }

            Item {
                height: 50
                width: parent.width
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

            Item {
                height: 50
                width: parent.width
            }
        }
}







Item {
    id: centralPane

    // Middle Panel - Text/Latex Editor
    Rectangle {
        anchors.fill: parent
        color: "#f5f5f5"

        TabBar {
            id: tabBar
            anchors.top: parent.top
            width: parent.width

            TabButton {
                text: "Text"
            }
            TabButton {
                text: "Code Latex"
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Sync the layout's active index with the TabBar's current index
            currentIndex: tabBar.currentIndex

            // Text Editor Page
            Rectangle {
                Column {
                    anchors.fill: parent
                    spacing: 10
                    TextArea {
                        id: textEditor
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: buttonRow.top
                        anchors.margins: 10
                        readOnly: true
                        wrapMode: Text.WordWrap

                        Component.onCompleted: {
                            text = "Text content will appear here...";
                        }
                    }
                    Row {
                        id: buttonRow
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 10

                        Button {
                            text: qsTr("Save")
                            // icon.source: "qrc:/Ressources/save.svg"
                            onClicked: console.log("Save Text")
                        }

                        Button {
                            text: qsTr("Edit")
                            // icon.source: "qrc:/Ressources/edit.svg"
                            onClicked: textEditor.readOnly = !textEditor.readOnly
                        }
                    }
                }
            }

            // Latex Editor Page
            Rectangle {
                Column {
                    anchors.fill: parent
                    spacing: 10

                    TextArea {
                        id: latexEditor
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: latexButtonRow.top
                        anchors.margins: 10
                        readOnly: true
                        wrapMode: Text.Wrap

                        Component.onCompleted: {
                            text = "LaTeX code will appear here...";
                        }
                    }

                    Row {
                        id: latexButtonRow
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 10

                        Button {
                            text: qsTr("Edit")
                            // icon.source: "qrc:/Ressources/edit.svg"
                            onClicked: latexEditor.readOnly = !latexEditor.readOnly
                        }
                    }
                }
            }
        }
    }
}




import QtQuick
import QtQuick.Controls

Rectangle {
    id: rightPane

    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 20
Item{
    id: controlSearch
                    
        // Search and Action Buttons
        Column {
            width: parent.width
            spacing: 10

            TextField {
                id: paperSearch
                width: parent.width
                placeholderText: "Search paper"
                //clearButtonEnabled: true
            }

            Row {
                width: parent.width
                spacing: 10

                Button {
                    ToolTip.text: "Summarize"
                    // icon.source: "qrc:/Ressources/ai-svgrepo-com.svg"
                    onClicked: console.log("Summarize")
                }

                Button {
                    ToolTip.text: "Refresh"
                    // icon.source: "qrc:/Ressources/refresh.svg"
                    onClicked: console.log("Refresh")
                }

                Button {
                    ToolTip.text: "Generate Report from selection"
                    // icon.source: "qrc:/Ressources/list.svg"
                    onClicked: console.log("Generate Report")
                }

                Button {
                    ToolTip.text: "Delete"
                    // icon.source: "qrc:/Ressources/trash-can.svg"
                    onClicked: console.log("Delete Paper")
                }
            }
        }

        // Progress section
        Column {
            width: parent.width
            spacing: 5

            ProgressBar {
                id: summarizeProgress
                value: 24
            }

            Label {
                text: "Summarize"
            }

            Label {
                text: "Elapsed Time"
            }
        }

        // Controls
        Row {
            width: parent.width
            spacing: 10

            CheckBox {
                text: "Select All"
            }

            Label {
                text: "Paper Number:"
            }
        }

        Row {
            width: parent.width
            spacing: 10

            Label {
                text: "Sort by"
            }

            ComboBox {
                model: ["Title", "Date", "Author", "Relevance"]
                width: parent.width * 0.4
            }

            CheckBox {
                text: "Descending"
            }
        }
    }
        // Paper List
        ListView {
            id: paperListView
            width: parent.width
            anchors.top: controlSearch.bottom
            anchors.bottom: parent.bottom

        }
    }
}
