pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
    property bool summarizing: false
    property bool ingesting: false
    property var reportJsons: []

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

    function deleteSelected() {
        if (selectedPapers.length === 0) {
            console.log("No papers selected");
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
    radius: 5
    color: Colors.surface0
    opacity: 1

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
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarizing...");
        }
        function onPaperFinished(hash, success, message) {
            summaryDbPane.paperProgress += 1;
            if (success)
                databaseManager.summaryModel.loadAll();
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = (success ? "✓ " : "✗ ") + message;
        }
        function onAllFinished(successCount, total) {
            summarizeTimer.stop();
            summaryDbPane.summarizing = false;
            summaryDbPane.totalPapers = 0;
            summaryDbPane.paperProgress = 0;
            summaryDbPane.elapsedTime = 0;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarize done: %1/%2").arg(successCount).arg(total);
        }
        function onSummaryStopped() {
            summarizeTimer.stop();
            summaryDbPane.summarizing = false;
            summaryDbPane.totalPapers = 0;
            summaryDbPane.paperProgress = 0;
            summaryDbPane.elapsedTime = 0;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = qsTr("Paper Number: %1").arg(databaseManager.summaryProxyModel.rowCount());
            if (summaryDbPane.statusBarLabel)
                summaryDbPane.statusBarLabel.text = qsTr("Summarization stopped by user");
        }
    }

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
            placeholderText: qsTr("Search paper")
            onTextChanged: databaseManager.summaryProxyModel.filterText = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Button {
                enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Summarize")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        console.log("No papers selected");
                        return;
                    }
                    summaryDbPane.totalPapers = summaryDbPane.selectedPapers.length;
                    summaryDbPane.paperProgress = 0;
                    summaryDbPane.elapsedTime = 0;
                    summaryDbPane.summarizing = true;
                    summarizeTimer.start();
                    summaryController.startSummarize(summaryDbPane.selectedPapers);
                    summaryDbPane.selectedPapers = [];
                }
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Refresh")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/refresh.svg"
                onClicked: {
                    databaseManager.summaryModel.loadAll();
                    databaseManager.citationsModel.loadAll();
                }
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Generate Report from selection")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/list.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        console.log("No papers selected");
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

            Button {
                enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete")
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: {
                    if (summaryDbPane.selectedPapers.length === 0) {
                        if (summaryDbPane.statusBarLabel)
                            summaryDbPane.statusBarLabel.text = qsTr("No papers selected");
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

            ProgressBar {
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
                }

                Button {
                    id: stopSummarize
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

        // Controls
        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            CheckBox {
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
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Sort by")
            }

            ComboBox {
                id: sortComboBox
                model: [qsTr("Title"), qsTr("Date"), qsTr("Pages"), qsTr("Model"), qsTr("Year"), qsTr("Citations")]
                Layout.fillWidth: true
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

            CheckBox {
                id: descendingCheckBox
                text: qsTr("Descending")
                onCheckedChanged: databaseManager.summaryProxyModel.sortDescending = checked
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

            ScrollBar.vertical: ScrollBar {
                id: paperScrollBar
                policy: ScrollBar.AsNeeded
                width: 10
                contentItem: Rectangle {
                    implicitWidth: 8
                    color: paperScrollBar.active ? Colors.overlay2 : Colors.surface2
                    radius: 4
                }
                background: Rectangle {
                    width: 8
                    color: "transparent"
                }
            }

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
                radius: 4
                color: mouseArea.containsMouse ? Colors.surface1 : Colors.surface0
                focus: summaryDbPane.activePaperHash === paperHash

                // Soft blurred halo behind the active card (the glow)
                Loader {
                    id: cardGlow
                    anchors.fill: parent
                    sourceComponent: Colors.glowHalo
                    onLoaded: item.source = card
                    visible: summaryDbPane.activePaperHash === paperHash
                }

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
                        CheckBox {
                            enabled: !summaryDbPane.summarizing && !summaryDbPane.ingesting
                            checked: summaryDbPane.selectedPapers.includes(paperHash)
                            icon.height: 8
                            font.pixelSize: 10
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

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            font.pixelSize: 12
                            text: qsTr("Year: %1 · %2 pages ").arg(year).arg(pagesNum)
                            font.family: Fonts.mono
                        }
                        Label {
                            font.pixelSize: 12
                            // "cited by N" only makes sense once OpenAlex has
                            // populated cites_count (0 = not fetched yet).
                            text: citesCount > 0 ? qsTr("· cited by %1").arg(citesCount) : ""
                            font.family: Fonts.mono
                            opacity: citesCount > 0 ? 0.8 : 0
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: formattedDate
                            font.family: Fonts.mono
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton {
                            icon.source: llmodel ? "qrc:Resources/circle-check.svg" : "qrc:Resources/circle-xmark.svg"

                            display: AbstractButton.IconOnly

                            // Define the internal icon size
                            icon.width: 20
                            icon.height: 20
                            icon.color: llmodel ? Colors.green : Colors.red

                            // Define how much space the button takes in the Layout
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24

                            // Optional: Align it vertically to the center of the row
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: llmodel
                            font.family: Fonts.mono
                            font.pixelSize: 12
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
            var result = helpers.generateReport(summaryDbPane.reportJsons, selectedFile.toString());
            if (result)
                console.log("Report saved to:", result);
            else
                console.warn("Report export failed");
        }
    }

    // Delete papers confirmation dialog
    Dialog {
        id: deletePapersDialog
        anchors.centerIn: parent
        modal: true
        width: 420
        title: qsTr("Delete Papers")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: deletePapersDialog.width
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
