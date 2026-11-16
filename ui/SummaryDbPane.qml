pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: summaryDbPane
    property int npapers: databaseManager.summaryModel.rowCount()
    property list<string> selectedPapers: []
    property string activePaperHash: ""
    property var statusBarLabel: null
    property int paperProgress: 0
    property int totalPapers: 0
    property int elapsedTime: 0
    property double summaryStartMs: 0
    property bool summarizing: false
    property bool ingesting: false
    property var reportJsons: []
    // Summary run report: failures ("title — cause") collected from
    // paperFinished, shown in one window when the run ends.
    property var summaryFailures: []
    property bool summaryWasStopped: false

    // Wall-clock duration since summaryStartMs, as "1h 02m", "3m 05s" or "42s".
    function summaryDuration() {
        var totalSec = Math.round((Date.now() - summaryStartMs) / 1000);
        if (totalSec >= 3600) {
            var h = Math.floor(totalSec / 3600);
            var m = Math.floor((totalSec % 3600) / 60);
            return h + "h " + m.toString().padStart(2, "0") + "m";
        }
        if (totalSec >= 60) {
            var min = Math.floor(totalSec / 60);
            var s = totalSec % 60;
            return min + "m " + s.toString().padStart(2, "0") + "s";
        }
        return totalSec + "s";
    }

    function addSelectedPaper(hash) {
        if (!selectedPapers.includes(hash) && hash !== "") {
            selectedPapers = selectedPapers.concat([hash]);
        }
    }

    function removeSelectedPaper(hash) {
        var result = [];
        for (var i = 0; i < selectedPapers.length; i++) {
            if (selectedPapers[i] !== hash) {
                result.push(selectedPapers[i]);
            }
        }
        selectedPapers = result;
    }

    function showNoSelection() {
        messageBox.showInfo(qsTr("No Papers Selected"), qsTr("Select at least one paper first."));
    }

    function deleteSelected() {
        if (selectedPapers.length === 0) {
            showNoSelection();
            return;
        }
        console.log("Deleting", selectedPapers.length, "papers");
        databaseManager.removeByHashes(selectedPapers);
        var imgPath = configManager.projectImgPath(configManager.lastProject);
        configManager.deleteImages(selectedPapers, imgPath);
        selectedPapers = [];
        databaseManager.summaryModel.loadAll();
        numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
    }

    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge
    color: Colors.surface0
    // Never draw over the neighbouring pane when dragged very narrow.
    clip: true

    Connections {
        target: engineController
        function onIngestionStarted() {
            summaryDbPane.ingesting = true;
        }
        function onIngestionFinished() {
            summaryDbPane.ingesting = false;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
        }
    }

    // Summarizer feedback
    Connections {
        target: summaryController
        function onSummaryStarted() {
            summaryDbPane.summaryFailures = [];
            summaryDbPane.summaryWasStopped = false;
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarizing...");
        }
        function onPaperFinished(hash, title, success, message) {
            summaryDbPane.paperProgress += 1;
            if (success)
                databaseManager.summaryModel.loadAll();
            // The paper interrupted by Stop is not a failure.
            else if (!summaryDbPane.summaryWasStopped)
                summaryDbPane.summaryFailures = summaryDbPane.summaryFailures.concat([title + " — " + message]);
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = (success ? "✓ " : "✗ ") + title + " — " + message;
        }
        function onAllFinished(successCount, total) {
            summarizeTimer.stop();
            summaryDbPane.summarizing = false;
            summaryDbPane.totalPapers = 0;
            summaryDbPane.paperProgress = 0;
            summaryDbPane.elapsedTime = 0;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
            // After Stop, allFinished still fires: keep the "stopped" status
            // and only report papers that failed before the stop.
            if (summaryDbPane.summaryWasStopped) {
                if (summaryDbPane.summaryFailures.length > 0)
                    messageBox.showError(qsTr("Summarization Stopped"), qsTr("%n paper(s) failed before the stop:", "", summaryDbPane.summaryFailures.length) + "\n\n• " + summaryDbPane.summaryFailures.join("\n• "));
                summaryDbPane.summaryFailures = [];
                return;
            }
            const duration = summaryDbPane.summaryDuration();
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarize done: %1/%2 in %3").arg(successCount).arg(total).arg(duration);
            const failures = summaryDbPane.summaryFailures;
            summaryDbPane.summaryFailures = [];
            if (failures.length === 0) {
                messageBox.showInfo(qsTr("Summarization Complete"), qsTr("%n paper(s) summarized in %1.", "", successCount).arg(duration));
            } else {
                let text = qsTr("%1 of %2 paper(s) summarized in %3.").arg(successCount).arg(total).arg(duration);
                text += "\n\n" + qsTr("%n paper(s) failed:", "", failures.length) + "\n\n• " + failures.join("\n• ");
                messageBox.showError(successCount === 0 ? qsTr("Summarization Failed") : qsTr("Summarization Partly Failed"), text);
            }
        }
        function onSummaryStopped() {
            summaryDbPane.summaryWasStopped = true;
            summarizeTimer.stop();
            summaryDbPane.summarizing = false;
            summaryDbPane.totalPapers = 0;
            summaryDbPane.paperProgress = 0;
            summaryDbPane.elapsedTime = 0;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarization stopped by user after %1").arg(summaryDbPane.summaryDuration());
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing

        StyledTextField {
            id: paperSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Search paper")
            onTextChanged: databaseManager.summaryProxyModel.filterText = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            StyledButton {
                enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                toolTip: qsTr("Summarize")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        summaryDbPane.showNoSelection();
                        return;
                    }
                    summaryDbPane.totalPapers = summaryDbPane.selectedPapers.length;
                    summaryDbPane.paperProgress = 0;
                    summaryDbPane.elapsedTime = 0;
                    summaryDbPane.summaryStartMs = Date.now();
                    summaryDbPane.summarizing = true;
                    summarizeTimer.start();
                    summaryController.startSummarize(summaryDbPane.selectedPapers);
                    summaryDbPane.selectedPapers = [];
                }
            }

            StyledButton {
                toolTip: qsTr("Refresh")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/refresh.svg"
                onClicked: {
                    databaseManager.summaryModel.loadAll();
                    databaseManager.citationsModel.loadAll();
                }
            }

            StyledButton {
                toolTip: qsTr("Generate Report from selection")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/list.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        summaryDbPane.showNoSelection();
                        return;
                    }
                    var jsons = [];
                    for (var i = 0; i < databaseManager.summaryProxyModel.rowCount(); i++) {
                        var hash = databaseManager.summaryProxyModel.get(i, "paperHash");
                        if (summaryDbPane.selectedPapers.includes(hash)) {
                            jsons.push(databaseManager.summaryProxyModel.get(i, "summaryJson"));
                        }
                    }
                    summaryDbPane.reportJsons = jsons;
                    reportSaveDialog.open();
                }
            }

            StyledButton {
                enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                toolTip: qsTr("Delete")
                destructive: true
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        summaryDbPane.showNoSelection();
                        return;
                    }
                    deletePapersDialog.open();
                }
            }
        }

        // Progress section
        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            visible: summaryDbPane.summarizing

            StyledProgressBar {
                id: summarizeProgress
                from: 0
                to: 100
                value: summaryDbPane.totalPapers > 0 ? (summaryDbPane.paperProgress / summaryDbPane.totalPapers) * 100 : 0
                Layout.fillWidth: true
            }

            RowLayout {
                Label {
                    text: qsTr("Summarizing %1/%2 papers").arg(summaryDbPane.paperProgress).arg(summaryDbPane.totalPapers)
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }

                Label {
                    text: {
                        var totalSec = Math.floor(summaryDbPane.elapsedTime / 10);
                        if (totalSec >= 3600) {
                            var h = Math.floor(totalSec / 3600);
                            var m = Math.floor((totalSec % 3600) / 60);
                            return qsTr("Elapsed Time: %1h %2m").arg(h).arg(m.toString().padStart(2, "0"));
                        } else if (totalSec >= 60) {
                            var m = Math.floor(totalSec / 60);
                            var s = totalSec % 60;
                            return qsTr("Elapsed Time: %1m %2s").arg(m).arg(s.toString().padStart(2, "0"));
                        } else {
                            return qsTr("Elapsed Time: %1s").arg(totalSec);
                        }
                    }
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }

                StyledButton {
                    id: stopSummarize
                    destructive: true
                    text: qsTr("Stop")
                    icon.source: "qrc:/Resources/circle-xmark.svg"
                    onClicked: {
                        summaryController.stopSummarize();
                    }
                }
            }
        }

        Timer {
            id: summarizeTimer
            interval: 100
            running: false
            repeat: true
            onTriggered: {
                summaryDbPane.elapsedTime += 1;
            }
        }

        // Controls: selection/count and sorting side by side, stacked when the
        // pane is too narrow (split handle dragged left, display scaling).
        GridLayout {
            Layout.fillWidth: true
            columns: summaryDbPane.width < 560 ? 1 : 2
            columnSpacing: Style.spacing
            rowSpacing: 5

            RowLayout {
                spacing: 5
                Layout.fillWidth: true

                StyledCheckBox {
                    id: selectAllCheckbox
                    enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                    text: qsTr("Select All")
                    checked: false
                    onCheckedChanged: {
                        if (checked) {
                            for (var i = 0; i < databaseManager.summaryProxyModel.rowCount(); i++) {
                                var hash = databaseManager.summaryProxyModel.get(i, "paperHash");
                                summaryDbPane.addSelectedPaper(hash);
                            }
                        } else {
                            summaryDbPane.selectedPapers = [];
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }

                Label {
                    id: numberPaper
                    text: qsTr("Documents: %1").arg(summaryDbPane.npapers)
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                spacing: 5
                Layout.fillWidth: true

                Label {
                    text: qsTr("Sort by")
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                }

                StyledComboBox {
                    id: sortComboBox
                    model: [qsTr("Title"), qsTr("Date"), qsTr("Pages"), qsTr("Model"), qsTr("Year"), qsTr("Citations")]
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    onCurrentIndexChanged: {
                        // TitleRole=259, DateRole=267, PagesNumRole=261, LlmModelRole=265,
                        // CitesCountRole=269
                        var roleMap = {
                            0: 259  // Title
                            ,
                            1: 267  // Date
                            ,
                            2: 261  // PagesNum
                            ,
                            3: 265   // LlmModel
                            ,
                            4: 263 // Year
                            ,
                            5: 269   // CitesCount (papers that cite this paper)
                        };
                        databaseManager.summaryProxyModel.sortRole = roleMap[currentIndex] || 259;
                    }
                }

                StyledCheckBox {
                    id: descendingCheckBox
                    text: qsTr("Descending")
                    onCheckedChanged: databaseManager.summaryProxyModel.sortDescending = checked
                }
            }
        }
        // Paper List
        ListView {
            id: paperListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.DragAndOvershootBounds
            Layout.minimumHeight: parent.height * 0.3
            focus: true
            highlight: Rectangle {
                color: "transparent"
            }

            // Sync currentIndex with activePaperHash so arrow keys navigate
            // through the list and highlight the focused row.
            Connections {
                target: summaryDbPane
                function onActivePaperHashChanged() {
                    for (var i = 0; i < databaseManager.summaryProxyModel.rowCount(); i++) {
                        if (databaseManager.summaryProxyModel.get(i, "paperHash") === summaryDbPane.activePaperHash) {
                            paperListView.currentIndex = i;
                            break;
                        }
                    }
                }
            }
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < databaseManager.summaryProxyModel.rowCount()) {
                    var hash = databaseManager.summaryProxyModel.get(currentIndex, "paperHash");
                    if (hash && hash !== summaryDbPane.activePaperHash)
                        summaryDbPane.activePaperHash = hash;
                }
            }

            ScrollBar.vertical: StyledScrollBar {}

            model: databaseManager.summaryProxyModel
            spacing: 5

            delegate: Rectangle {
                id: card
                required property string title
                required property string authors
                required property int pagesNum
                required property string llmodel
                required property string formattedDate
                required property string paperHash
                required property int citesCount
                required property string year
                width: paperListView.width - 10
                height: 120
                border.width: summaryDbPane.activePaperHash === paperHash ? 2 : 1
                border.color: summaryDbPane.activePaperHash === paperHash ? Colors.primary : Colors.surface1
                radius: Style.radiusSmall
                color: mouseArea.containsMouse ? Colors.surface1 : Colors.surface0
                focus: summaryDbPane.activePaperHash === paperHash


                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        summaryDbPane.activePaperHash = paperHash;
                        paperListView.forceActiveFocus();
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 1
                    RowLayout {
                        StyledCheckBox {
                            enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                            checked: summaryDbPane.selectedPapers.includes(paperHash)
                            onCheckedChanged: {
                                if (checked) {
                                    summaryDbPane.addSelectedPaper(paperHash);
                                } else {
                                    summaryDbPane.removeSelectedPaper(paperHash);
                                }
                            }
                        }
                        Label {
                            text: title || ""
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.bold: true
                        }
                    }

                    Label {
                        text: authors || qsTr("Please summarise for authors name")
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        opacity: 0.7
                    }

                    // Labels may shrink (elided) so a narrow pane never pushes
                    // text past the card border.
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            font.pixelSize: Style.fontSmall
                            text: qsTr("Year: %1 · %2 pages ").arg(year).arg(pagesNum)
                            font.family: Fonts.mono
                            elide: Text.ElideRight
                            Layout.minimumWidth: 0
                        }
                        Label {
                            font.pixelSize: Style.fontSmall
                            // "cited by N" only makes sense once OpenAlex has
                            // populated cites_count (0 = not fetched yet).
                            text: citesCount > 0 ? qsTr("· cited by %1").arg(citesCount) : ""
                            font.family: Fonts.mono
                            opacity: citesCount > 0 ? 0.8 : 0
                            elide: Text.ElideRight
                            Layout.minimumWidth: 0
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: formattedDate
                            font.family: Fonts.mono
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideLeft
                            Layout.minimumWidth: 0
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        // Summary status (display only, not a button)
                        IconImage {
                            source: llmodel ? "qrc:Resources/circle-check.svg" : "qrc:Resources/circle-xmark.svg"
                            sourceSize.width: 20
                            sourceSize.height: 20
                            color: llmodel ? Colors.green : Colors.red
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: llmodel
                            font.family: Fonts.mono
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideMiddle
                            Layout.minimumWidth: 0
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: reportSaveDialog
        title: qsTr("Save Report as PDF")
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF Files (*.pdf)"]
        onAccepted: {
            var result = helpers.exportPdf(summaryDbPane.reportJsons, selectedFile.toString());
            if (result)
                messageBox.showInfo(qsTr("Report Saved"), qsTr("The report was saved to:\n%1").arg(result));
            else
                messageBox.showError(qsTr("Report Export Failed"), qsTr("The report could not be saved. See the log for details."));
        }
    }

    MessageBox {
        id: messageBox
    }

    // Delete papers confirmation dialog
    StyledDialog {
        id: deletePapersDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Delete Papers")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: deletePapersDialog.availableWidth
            text: qsTr("Delete the selected paper(s)? This cannot be undone.")
            wrapMode: Text.WordWrap
        }
        onAccepted: summaryDbPane.deleteSelected()
        onRejected: {
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Deletion cancelled");
        }
    }
}
