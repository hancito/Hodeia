pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic

Rectangle {
    id: rightPane
    property var activeTheme: ({})
    border.width: 1
    radius: 5
    color: Colors.surface0
    opacity: 1

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
            onTextChanged: databaseManager.summaryProxyModel.filterText = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Button {
                ToolTip.visible: hovered
                ToolTip.text: "Summarize"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: console.log("Summarize")
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: "Refresh"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/refresh.svg"
                onClicked: {
                    databaseManager.summaryModel.loadAll();
                    console.log(databaseManager.summaryModel);
                }
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: "Generate Report from selection"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/list.svg"
                onClicked: console.log("Generate Report")
            }

            Button {
                ToolTip.visible: hovered
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
                id: sortComboBox
                model: ["Title", "Date", "Pages", "Model"]
                Layout.fillWidth: true
                onCurrentIndexChanged: {
                    var roleMap = {
                        0: 1,
                        1: 2,
                        2: 3,
                        3: 4
                    };
                    databaseManager.summaryProxyModel.sortRole = roleMap[currentIndex] || 1;
                }
            }

            CheckBox {
                id: descendingCheckBox
                text: "Descending"
                onCheckedChanged: databaseManager.summaryProxyModel.sortDescending = checked
            }
        }

        // Paper List
        QtQuick.ListView {
            id: paperListView
            Layout.fillWidth: true
            Layout.fillHeight: true

            boundsBehavior: Flickable.DragAndOvershootBounds
            Layout.minimumHeight: parent.height * 0.3

            model: databaseManager.summaryProxyModel

            delegate: Rectangle {
                width: paperListView.width
                height: 70
                border.width: 1
                border.color: Colors.surface1
                radius: 4
                color: mouseArea.containsMouse ? Colors.surface1 : Colors.surface0

                readonly property var rowItem: databaseManager.summaryProxyModel.get(index)

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Label {
                        text: rowItem.title || ""
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.bold: true
                    }

                    Label {
                        text: rowItem.authors || ""
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        opacity: 0.7
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: rowItem.year || ""
                            opacity: 0.5
                            font.pixelSize: 11
                        }

                        Label {
                            text: rowItem.journal || ""
                            opacity: 0.5
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: rowItem.pagesNum ? rowItem.pagesNum + " p." : ""
                            opacity: 0.5
                            font.pixelSize: 11
                        }

                        Label {
                            text: rowItem.formattedDate || ""
                            opacity: 0.5
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
