pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Pdf

Rectangle {
    id: displayPane
    property var activeTheme: ({})
    property string activePaperHash: ""
    color: Colors.surface0
    opacity: 1
    border.width: 1
    radius: 5

    // Resolve the raw JSON summary for the active paper
    readonly property string activeSummaryJson: {
        if (activePaperHash === "")
            return "";
        for (var i = 0; i < databaseManager.summaryProxyModel.rowCount(); i++) {
            var hash = databaseManager.summaryProxyModel.get(i, "paperHash");
            if (hash === activePaperHash)
                return databaseManager.summaryProxyModel.get(i, "summaryJson");
        }
        return "";
    }

    // Reset page spinner and zoom when the active paper changes
    Connections {
        target: displayPane
        function onActivePaperHashChanged() {
            pageSpinner.value = 1;
            pdfTab.pdfZoomLevel = 1;
        }
    }

    // Resolve image paths for the active paper
    readonly property list<string> pageImages: {
        if (activePaperHash === "")
            return [];
        let imgPath = configManager.projectImgPath(configManager.lastProject);
        return modelHelper.getImagesForHash(activePaperHash, imgPath);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 5
        anchors.leftMargin: 5
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 15

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton {
                text: qsTr("Text")
            }
            TabButton {
                text: qsTr("References")
            }
            TabButton {
                text: qsTr("PDF")
            }
        }

        // Page navigation row (visible only on PDF tab)
        RowLayout {
            visible: tabBar.currentIndex === 3
            Layout.fillWidth: true
            spacing: 8
            opacity: displayPane.pageImages.length > 0 ? 1 : 0

            Label {
                text: qsTr("Page")
                opacity: 0.7
            }
            ToolButton {
                icon.source: "qrc:/Resources/arrow-circle-left.svg"
                icon.width: 20
                icon.height: 20
                enabled: pageSpinner.value > pageSpinner.from
                onClicked: pageSpinner.value -= 1
            }
            SpinBox {
                id: pageSpinner
                from: 1
                to: Math.max(displayPane.pageImages.length, 1)
                value: 1
                editable: true
            }

            Label {
                text: "/" + displayPane.pageImages.length
                opacity: 0.7
            }
            ToolButton {
                icon.source: "qrc:/Resources/arrow-circle-right.svg"
                icon.width: 20
                icon.height: 20
                enabled: pageSpinner.value < pageSpinner.to
                onClicked: pageSpinner.value += 1
            }

            Item {
                Layout.fillWidth: true
            }

            // Zoom controls
            Label {
                text: qsTr("Zoom")
                opacity: 0.7
            }
            ToolButton {
                icon.source: "qrc:/Resources/magnifying-glass-minus.svg"
                icon.width: 20
                icon.height: 20
                enabled: pdfTab.pdfZoomLevel > 0.25
                onClicked: pdfTab.pdfZoomLevel = Math.max(0.25, pdfTab.pdfZoomLevel - 0.25)
            }
            Label {
                id: zoomLabel
                text: Math.round(pdfTab.pdfZoomLevel * 100) + "%"
                font.family: Fonts.mono
                font.pixelSize: 12
                opacity: 0.7
            }
            ToolButton {
                id: zoomInBtn
                icon.source: "qrc:/Resources/magnifying-glass-plus.svg"
                icon.width: 20
                icon.height: 20
                enabled: pdfTab.pdfZoomLevel < 5
                onClicked: pdfTab.pdfZoomLevel = Math.min(5, pdfTab.pdfZoomLevel + 0.25)
            }
            ToolButton {
                icon.source: "qrc:/Resources/arrow-rotate-back.svg"
                icon.width: 20
                icon.height: 20
                ToolTip.visible: hovered
                ToolTip.text: "Reset zoom"
                onClicked: pdfTab.pdfZoomLevel = 1
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
                    anchors.rightMargin: 5
                    anchors.leftMargin: 5
                    spacing: 10
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        TextArea {
                            id: textEditor
                            readOnly: true
                            textFormat: TextEdit.MarkdownText
                            font.pointSize: 12
                            wrapMode: TextEdit.WordWrap
                            text: displayPane.activeSummaryJson ? modelHelper.summaryToMarkdown(displayPane.activeSummaryJson) : "Please make a summary of this paper"
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
                            onClicked: console.log(displayPane.activeSummaryJson)
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
                id: refTab
                ColumnLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 5
                    anchors.leftMargin: 5
                    spacing: 10
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        TextArea {
                            id: refEditor
                            readOnly: true
                            textFormat: TextEdit.MarkdownText
                            font.pointSize: 12
                            wrapMode: TextEdit.WordWrap
                            placeholderText: "Select a paper to see its LaTeX summary"
                            text: displayPane.activeSummaryJson ? modelHelper.summaryToRef(displayPane.activeSummaryJson) : "Please make a summary of this paper"
                        }
                    }
                }
            }
            // ── PDF tab ─────────────────────────────────────
            Item {
                id: pdfTab
                property real pdfZoomLevel: 1
                clip: true

                Flickable {
                    id: pdfFlickable
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: pdfImageItem.width * pdfImageItem.scale
                    contentHeight: pdfImageItem.height * pdfImageItem.scale

                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {}

                    Image {
                        id: pdfImageItem
                        width: pdfTab.width
                        height: pdfTab.height
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        scale: pdfTab.pdfZoomLevel
                        source: {
                            if (displayPane.pageImages.length > 0)
                                "file://" + displayPane.pageImages[Math.max(pageSpinner.value - 1, 0)];
                            else
                                "";
                        }
                    }
                }

                // Empty state
                Label {
                    anchors.centerIn: parent
                    opacity: displayPane.pageImages.length === 0 ? 0.5 : 0
                    text: displayPane.activePaperHash === "" ? "Select a paper to view its pages" : "No page images found"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
