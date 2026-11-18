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
    // Never draw over the neighbouring pane when dragged very narrow.
    clip: true

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

    // Give the PDF tab the keyboard focus when it is shown, so its
    // navigation keys work without clicking the page first.
    Connections {
        target: tabBar
        function onCurrentIndexChanged() {
            if (tabBar.currentIndex === 3)
                pdfTab.forceActiveFocus();
        }
    }

    // Zoom shortcuts (standard sequences follow the keyboard layout: Ctrl+=
    // / Ctrl++ / Ctrl+- on QWERTY and AZERTY alike).  Active only while the
    // PDF tab is shown.
    Shortcut {
        sequences: [StandardKey.ZoomIn]
        enabled: tabBar.currentIndex === 3 && displayPane.pageImages.length > 0
        onActivated: pdfTab.setZoom(pdfTab.pdfZoomLevel + 0.25)
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        enabled: tabBar.currentIndex === 3 && displayPane.pageImages.length > 0
        onActivated: pdfTab.setZoom(pdfTab.pdfZoomLevel - 0.25)
    }
    Shortcut {
        // "Ctrl+à": the 0 key on an AZERTY keyboard.
        sequences: ["Ctrl+0", "Ctrl+à"]
        enabled: tabBar.currentIndex === 3 && displayPane.pageImages.length > 0
        onActivated: pdfTab.setZoom(1)
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

        // Page navigation bar (visible only on PDF tab): page counter
        // centred, zoom controls on the right.
        // A RowLayout rather than anchors: with anchors.centerIn the counter
        // overlapped the zoom controls once the pane got narrow.  The left
        // placeholder mirrors the zoom controls' width to keep the counter
        // centred, and is the first thing to shrink when space runs out.
        RowLayout {
            visible: tabBar.currentIndex === 3
            Layout.fillWidth: true
            opacity: displayPane.pageImages.length > 0 ? 1 : 0
            spacing: 0

            Item {
                Layout.preferredWidth: zoomControls.implicitWidth
                Layout.minimumWidth: 0
            }
            Item {
                Layout.fillWidth: true
            }

            RowLayout {
                id: pageCounter
                spacing: 8

                StyledSpinBox {
                    id: pageSpinner
                    from: 1
                    to: Math.max(displayPane.pageImages.length, 1)
                    value: 1
                    editable: true
                    // A new page opens at its top, whatever the previous page's
                    // scroll position (it used to open where the last one was).
                    onValueChanged: pdfFlickable.contentY = 0
                }
                Label {
                    text: "/" + displayPane.pageImages.length + " pages"
                    opacity: 0.7
                }
            }

            Item {
                Layout.fillWidth: true
            }

            RowLayout {
                id: zoomControls
                spacing: 8

                Label {
                    text: qsTr("Zoom")
                    opacity: 0.7
                    // Optional label: dropped first when the pane is narrow.
                    visible: displayPane.width > 480
                }
                StyledButton {
                    flat: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/Resources/magnifying-glass-minus.svg"
                    icon.width: 20
                    icon.height: 20
                    focusPolicy: Qt.NoFocus
                    toolTip: qsTr("Zoom out (Ctrl+-)")
                    enabled: pdfTab.pdfZoomLevel > 0.25
                    onClicked: pdfTab.setZoom(pdfTab.pdfZoomLevel - 0.25)
                }
                Label {
                    id: zoomLabel
                    text: Math.round(pdfTab.pdfZoomLevel * 100) + "%"
                    font.family: Fonts.mono
                    font.pixelSize: Style.fontSmall
                    opacity: 0.7
                }
                StyledButton {
                    id: zoomInBtn
                    flat: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/Resources/magnifying-glass-plus.svg"
                    icon.width: 20
                    icon.height: 20
                    focusPolicy: Qt.NoFocus
                    toolTip: qsTr("Zoom in (Ctrl++)")
                    enabled: pdfTab.pdfZoomLevel < pdfTab.maxZoom
                    onClicked: pdfTab.setZoom(pdfTab.pdfZoomLevel + 0.25)
                }
                StyledButton {
                    flat: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/Resources/arrow-rotate-back.svg"
                    icon.width: 20
                    icon.height: 20
                    focusPolicy: Qt.NoFocus
                    toolTip: qsTr("Reset zoom (Ctrl+0)")
                    onClicked: pdfTab.setZoom(1)
                }
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
                        // The Basic style overlays the scroll bar on the
                        // content: reserve its width so it never covers text.
                        // Always reserved (not only when visible) to avoid a
                        // rewrap → height change → bar toggling loop.
                        rightPadding: summaryScrollBar.width
                        // Without this the content width defaults to the
                        // TextEdit's *unwrapped* implicit width (thousands of
                        // px): the view then scrolls sideways (wheel, touchpad,
                        // selection drag) and pushes the text under the bar.
                        contentWidth: availableWidth
                        ScrollBar.vertical: StyledScrollBar {
                            id: summaryScrollBar
                            parent: scrollView
                            x: scrollView.width - width
                            y: scrollView.topPadding
                            height: scrollView.availableHeight
                        }

                        TextEdit {
                            id: textEditor
                            width: scrollView.availableWidth-10
                            textFormat: TextEdit.RichText
                            color: Colors.text
                            font.family: Fonts.text
                            font.pixelSize: Style.fontReading
                            // Hinted glyphs render wider than the metrics the
                            // rich-text layout wraps with: lines then spill past
                            // the TextEdit's right edge (clipped / under the
                            // scroll bar).  Unhinted, layout and render agree.
                            font.hintingPreference: Font.PreferNoHinting
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            text: displayPane.activeSummaryJson ? helpers.summaryToHtml(displayPane.activeSummaryJson, displayPane.activePaperHash, configManager.zoteroPriority) : qsTr("Please make a summary of this paper")
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
                                    messageBox.showInfo(qsTr("No Summary"), qsTr("This paper has no summary to export yet. Summarize it first."));
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
                            // Keep the text clear of the overlaid scroll bar
                            // (see the summary tab).
                            rightPadding: refScrollBar.width + 4
                            contentWidth: availableWidth // no sideways scroll
                            ScrollBar.vertical: StyledScrollBar {
                                id: refScrollBar
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
                                font.hintingPreference: Font.PreferNoHinting // see summary tab
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
                activeFocusOnTab: true

                function goToPage(n) {
                    pageSpinner.value = Math.max(pageSpinner.from, Math.min(pageSpinner.to, n));
                }
                function setZoom(z) {
                    pdfZoomLevel = Math.max(0.25, Math.min(maxZoom, z));
                }
                // Vertical scroll by dy pixels, kept within the page.
                function scrollBy(dy) {
                    const maxY = Math.max(0, pdfFlickable.contentHeight - pdfFlickable.height);
                    pdfFlickable.contentY = Math.max(0, Math.min(maxY, pdfFlickable.contentY + dy));
                }

                // Page navigation.  Handled here (not as window shortcuts) so
                // the arrow keys keep working in text fields elsewhere.
                Keys.onPressed: event => {
                    if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
                        return;
                    const step = pdfFlickable.height * 0.15;
                    switch (event.key) {
                    case Qt.Key_Left:
                    case Qt.Key_PageUp:
                        goToPage(pageSpinner.value - 1);
                        break;
                    case Qt.Key_Right:
                    case Qt.Key_PageDown:
                        goToPage(pageSpinner.value + 1);
                        break;
                    case Qt.Key_Home:
                        goToPage(pageSpinner.from);
                        break;
                    case Qt.Key_End:
                        goToPage(pageSpinner.to);
                        break;
                    case Qt.Key_Up:
                        scrollBy(-step);
                        break;
                    case Qt.Key_Down:
                        scrollBy(step);
                        break;
                    default:
                        return;
                    }
                    event.accepted = true;
                }

                // Natural size of the page PNG (0 while nothing is loaded).
                // sourceSize, not implicitWidth/Height: with PreserveAspectFit
                // Image recomputes its implicit size from the width/height
                // bound below, which depend on these values (binding loop).
                readonly property real imageW: pdfImageItem.sourceSize.width
                readonly property real imageH: pdfImageItem.sourceSize.height
                // fitW/fitH: the page's size after PreserveAspectFit into the
                // viewport (zoom 1).  Falls back to the viewport size before
                // the image loads.
                readonly property real fitScale: (imageW > 0 && imageH > 0) ? Math.min(pdfFlickable.width / imageW, pdfFlickable.height / imageH) : 0
                readonly property real fitW: fitScale > 0 ? imageW * fitScale : pdfFlickable.width
                readonly property real fitH: fitScale > 0 ? imageH * fitScale : pdfFlickable.height
                // Zoom cap: one PNG pixel per physical screen pixel — beyond
                // that the page only gets blurrier.  Never below 1 (a small
                // or low-resolution PNG still fills the viewport).  Uncapped
                // (5) while a page is loading, so switching pages keeps the
                // zoom level.
                readonly property real maxZoom: (pdfImageItem.status === Image.Ready && fitScale > 0) ? Math.max(1, 1 / (fitScale * Screen.devicePixelRatio)) : 5
                onMaxZoomChanged: {
                    if (pdfZoomLevel > maxZoom)
                        pdfZoomLevel = maxZoom;
                }
                readonly property real zoomedWidth: pdfTab.fitW * pdfTab.pdfZoomLevel
                readonly property real zoomedHeight: pdfTab.fitH * pdfTab.pdfZoomLevel
                // Content must never shrink below the viewport, otherwise a
                // portrait page (fit < viewport) or the pre-load state collapses
                // the Flickable content and clips the image away.
                readonly property real effContentW: Math.max(pdfTab.zoomedWidth, pdfFlickable.width)
                readonly property real effContentH: Math.max(pdfTab.zoomedHeight, pdfFlickable.height)

                // Page arrows on either side of the page, in their own
                // columns so they never cover it when zoomed in.
                StyledButton {
                    id: prevPageButton
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: displayPane.pageImages.length > 0 ? 1 : 0
                    flat: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/Resources/arrow-circle-left.svg"
                    icon.width: 24
                    icon.height: 24
                    focusPolicy: Qt.NoFocus // keep the keyboard focus on the page
                    toolTip: qsTr("Previous page (←, Page Up)")
                    enabled: pageSpinner.value > pageSpinner.from
                    onClicked: pdfTab.goToPage(pageSpinner.value - 1)
                }

                Flickable {
                    id: pdfFlickable
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: prevPageButton.right
                    anchors.right: nextPageButton.left
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: pdfTab.effContentW
                    contentHeight: pdfTab.effContentH

                    ScrollBar.vertical: StyledScrollBar {}
                    ScrollBar.horizontal: StyledScrollBar {}

                    // A click on the page (e.g. after typing a page number)
                    // gives the keyboard back to the page navigation.
                    TapHandler {
                        onTapped: pdfTab.forceActiveFocus()
                    }

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

                StyledButton {
                    id: nextPageButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: displayPane.pageImages.length > 0 ? 1 : 0
                    flat: true
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/Resources/arrow-circle-right.svg"
                    icon.width: 24
                    icon.height: 24
                    focusPolicy: Qt.NoFocus // keep the keyboard focus on the page
                    toolTip: qsTr("Next page (→, Page Down)")
                    enabled: pageSpinner.value < pageSpinner.to
                    onClicked: pdfTab.goToPage(pageSpinner.value + 1)
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
            var result = helpers.exportPdf([displayPane.activeSummaryJson], selectedFile.toString(), [displayPane.activePaperHash], configManager.zoteroPriority);
            if (result)
                messageBox.showInfo(qsTr("PDF Saved"), qsTr("The summary was saved to:\n%1").arg(result));
            else
                messageBox.showError(qsTr("PDF Export Failed"), qsTr("The summary could not be saved. See the log for details."));
        }
    }

    MessageBox {
        id: messageBox
    }
}
