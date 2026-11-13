pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: displayPane
    property string activePaperHash: ""
    color: Colors.surface0
    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge

    // Resolve the raw JSON summary for the active paper
    readonly property string activeSummaryJson: {
        // Read version so the binding re-evaluates after loadAll(): with a
        // single paper activePaperHash never changes, so nothing else would
        // refresh the displayed summary.
        var v = databaseManager.summaryModel.version;
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
        return helpers.getImagesForHash(activePaperHash, imgPath);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.paneMargin

        StyledTabBar {
            id: tabBar
            Layout.fillWidth: true
            StyledTabButton {
                text: qsTr("Summary")
            }
            StyledTabButton {
                text: qsTr("References (Cited)")
            }
            StyledTabButton {
                text: qsTr("Cited by")
            }
            StyledTabButton {
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
            StyledButton {
                flat: true
                display: AbstractButton.IconOnly
                icon.source: "qrc:/Resources/arrow-circle-left.svg"
                icon.width: 20
                icon.height: 20
                enabled: pageSpinner.value > pageSpinner.from
                onClicked: pageSpinner.value -= 1
            }
            StyledSpinBox {
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
            StyledButton {
                flat: true
                display: AbstractButton.IconOnly
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
            StyledButton {
                flat: true
                display: AbstractButton.IconOnly
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
                font.pixelSize: Style.fontSmall
                opacity: 0.7
            }
            StyledButton {
                flat: true
                display: AbstractButton.IconOnly
                id: zoomInBtn
                icon.source: "qrc:/Resources/magnifying-glass-plus.svg"
                icon.width: 20
                icon.height: 20
                enabled: pdfTab.pdfZoomLevel < 5
                onClicked: pdfTab.pdfZoomLevel = Math.min(5, pdfTab.pdfZoomLevel + 0.25)
            }
            StyledButton {
                flat: true
                display: AbstractButton.IconOnly
                icon.source: "qrc:/Resources/arrow-rotate-back.svg"
                icon.width: 20
                icon.height: 20
                toolTip: qsTr("Reset zoom")
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
                        id: scrollView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical: StyledScrollBar {
                            parent: scrollView
                            x: scrollView.width - width
                            y: scrollView.topPadding
                            height: scrollView.availableHeight
                        }

                        TextEdit {
                            id: textEditor
                            width: scrollView.availableWidth
                            textFormat: TextEdit.RichText
                            color: Colors.text
                            font.family: Fonts.text
                            font.pixelSize: Style.fontReading
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            text: displayPane.activeSummaryJson ? helpers.summaryToHtml(displayPane.activeSummaryJson) : qsTr("Please make a summary of this paper")
                        }
                    }
                    RowLayout {
                        id: buttonRow
                        spacing: 10
                        Layout.fillWidth: true

                        Item {
                            Layout.fillWidth: true
                        }

                        StyledButton {
                            text: qsTr("PDF")
                            icon.width: 24
                            icon.height: 24
                            icon.source: "qrc:/Resources/file-pdf-solid-full.svg"
                            onClicked: {
                                if (!displayPane.activeSummaryJson) {
                                    console.warn("No summary to export");
                                    return;
                                }
                                pdfSaveDialog.open();
                            }
                        }
                    }
                }
            }

            // Shared markup for the "References (Cited)" and "Cited by" tabs.
            // The only difference between the two is which model method feeds
            // the TextArea, so it is parameterised by the `displayMethod`
            // property. APA formatting lives in CitationsModel (C++); reading
            // model.version makes the binding re-evaluate after loadAll().
            Component {
                id: referenceTabComponent
                Item {
                    // Set by the Loader that instantiates this component.
                    property string displayMethod: "cites"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 5
                        anchors.leftMargin: 5
                        spacing: 10
                        ScrollView {
                            id: refScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            ScrollBar.vertical: StyledScrollBar {
                                parent: refScroll
                                x: refScroll.width - width
                                y: refScroll.topPadding
                                height: refScroll.availableHeight
                            }
                            TextArea {
                                readOnly: true
                                textFormat: TextEdit.MarkdownText
                                hoverEnabled: true
                                selectByMouse: true
                                font.pixelSize: Style.fontReading
                                wrapMode: TextEdit.WordWrap
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                                    acceptedButtons: Qt.NoButton // Let the TextArea handle clicks and text selection
                                }
                                onLinkActivated: function (link) {
                                    Qt.openUrlExternally(link);
                                }
                                text: {
                                    var model = databaseManager.citationsModel;
                                    var v = model.version; // invalidate after loadAll()
                                    var s = displayMethod === "citedBy" ? model.citedByDisplayForHash(displayPane.activePaperHash) : model.citesDisplayForHash(displayPane.activePaperHash);
                                    return s ? s : qsTr("No references found for this paper");
                                }
                            }
                        }
                    }
                }
            }

            Loader {
                id: refTab
                active: true
                sourceComponent: referenceTabComponent
                onLoaded: item.displayMethod = "cites"
            }

            Loader {
                id: refTab2
                active: true
                sourceComponent: referenceTabComponent
                onLoaded: item.displayMethod = "citedBy"
            }
            // ── PDF tab ─────────────────────────────────────
            Item {
                id: pdfTab
                property real pdfZoomLevel: 1
                clip: true

                // fitW/fitH: the image's size after PreserveAspectFit into the
                // viewport (zoom 1). Derived from nativeWidth/Height (not the
                // bound width/height, which would be circular). Falls back to
                // the viewport size before the image loads, because
                // nativeWidth is undefined (not 0) and undefined <= 0 is false
                // in JS, which would otherwise produce NaN.
                readonly property real fitW: {
                    if (!pdfImageItem.nativeWidth || !pdfImageItem.nativeHeight)
                        return pdfTab.width;
                    return pdfImageItem.nativeWidth * Math.min(pdfTab.width / pdfImageItem.nativeWidth, pdfTab.height / pdfImageItem.nativeHeight);
                }
                readonly property real fitH: {
                    if (!pdfImageItem.nativeWidth || !pdfImageItem.nativeHeight)
                        return pdfTab.height;
                    return pdfImageItem.nativeHeight * Math.min(pdfTab.width / pdfImageItem.nativeWidth, pdfTab.height / pdfImageItem.nativeHeight);
                }
                readonly property real zoomedWidth: pdfTab.fitW * pdfTab.pdfZoomLevel
                readonly property real zoomedHeight: pdfTab.fitH * pdfTab.pdfZoomLevel
                // Content must never shrink below the viewport, otherwise a
                // portrait page (fit < viewport) or the pre-load state collapses
                // the Flickable content and clips the image away.
                readonly property real effContentW: Math.max(pdfTab.zoomedWidth, pdfFlickable.width)
                readonly property real effContentH: Math.max(pdfTab.zoomedHeight, pdfFlickable.height)

                Flickable {
                    id: pdfFlickable
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: pdfTab.effContentW
                    contentHeight: pdfTab.effContentH

                    ScrollBar.vertical: StyledScrollBar {}
                    ScrollBar.horizontal: StyledScrollBar {}

                    Image {
                        id: pdfImageItem
                        // Center the zoomed box within the (viewport-or-larger)
                        // content area so the page is always visible.
                        x: (pdfFlickable.contentWidth - pdfTab.zoomedWidth) / 2
                        y: (pdfFlickable.contentHeight - pdfTab.zoomedHeight) / 2
                        width: pdfTab.zoomedWidth
                        height: pdfTab.zoomedHeight
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        // helpers.localFileUrl() uses QUrl::fromLocalFile
                        // (C++) to build a correct file: URL for any platform —
                        // handles Windows drive letters, native separators, and
                        // spaces.  A bare "file://" + path breaks on Windows
                        // (backslashes, wrong slash count).
                        source: {
                            if (displayPane.pageImages.length === 0)
                                return "";
                            return helpers.localFileUrl(displayPane.pageImages[Math.max(pageSpinner.value - 1, 0)]);
                        }
                    }
                }

                // Empty state
                Label {
                    anchors.centerIn: parent
                    opacity: displayPane.pageImages.length === 0 ? 0.5 : 0
                    text: displayPane.activePaperHash === "" ? qsTr("Select a paper to view its pages") : qsTr("No page images found")
                    font.pixelSize: Style.fontBody
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    FileDialog {
        id: pdfSaveDialog
        title: qsTr("Save Summary as PDF")
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF Files (*.pdf)"]
        onAccepted: {
            var result = helpers.exportSummaryToPdf(displayPane.activeSummaryJson, selectedFile.toString());
            if (result)
                console.log("PDF saved to:", result);
            else
                console.warn("PDF export failed");
        }
    }
}
