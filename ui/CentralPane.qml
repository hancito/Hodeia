pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Pdf

Rectangle {
    id: centralPane
    property var activeTheme: ({})
    property string activePaperHash: ""
    color: Colors.surface0
    opacity: 1
    border.width: 1
    radius: 5

    // Reset page spinner and zoom when the active paper changes
    Connections {
        target: centralPane
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
        return configManager.getImagesForHash(activePaperHash, imgPath);
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
                text: qsTr("Latex")
            }
            TabButton {
                text: qsTr("PDF")
            }
        }

        // Page navigation row (visible only on PDF tab)
        RowLayout {
            visible: tabBar.currentIndex === 2
            Layout.fillWidth: true
            spacing: 8
            opacity: centralPane.pageImages.length > 0 ? 1 : 0

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
                to: Math.max(centralPane.pageImages.length, 1)
                value: 1
                editable: true
            }

            Label {
                text: "/" + centralPane.pageImages.length
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
                            wrapMode: TextEdit.WordWrap
                            font.family: Fonts.display

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
                    anchors.rightMargin: 5
                    anchors.leftMargin: 5
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
                            if (centralPane.pageImages.length > 0)
                                "file://" + centralPane.pageImages[Math.max(pageSpinner.value - 1, 0)];
                            else
                                "";
                        }
                    }

                    // Mouse wheel zoom (Ctrl + wheel); pass-through for normal scroll
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        propagateComposedEvents: true
                        onWheel: wheel => {
                            if (wheel.modifiers & Qt.ControlModifier) {
                                wheel.accepted = true;
                                let delta = wheel.angleDelta.y > 0 ? 0.15 : -0.15;
                                pdfTab.pdfZoomLevel = Math.min(5, Math.max(0.25, pdfTab.pdfZoomLevel + delta));
                            }
                        }
                    }
                }

                // Empty state
                Label {
                    anchors.centerIn: parent
                    opacity: centralPane.pageImages.length === 0 ? 0.5 : 0
                    text: centralPane.activePaperHash === "" ? "Select a paper to view its pages" : "No page images found"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
