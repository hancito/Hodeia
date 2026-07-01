pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: rightPane
    property int npapers: databaseManager.summaryModel.rowCount()
    property list<string> selectedPapers: []
    property string activePaperHash: ""
    property var statusBarLabel: null
    property int paperProgress: 0
    property int totalPapers: 0
    property int elapsedTime: 0
    property bool summarizing: false

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
        numberPaper.text = "Paper Number: " + databaseManager.summaryProxyModel.rowCount();
    }

    border.width: 1
    radius: 5
    color: Colors.surface0
    opacity: 1

    Connections {
        target: engineController
        function onIngestionFinished() {
            databaseManager.summaryModel.loadAll();
            numberPaper.text = "Paper Number: " + databaseManager.summaryProxyModel.rowCount();
        }
    }

    // Summarizer feedback
    Connections {
        target: summaryController
        function onPaperFinished(hash, success, message) {
            rightPane.paperProgress += 1;
            if (rightPane.statusBarLabel)
                rightPane.statusBarLabel.text = (success ? "✓ " : "✗ ") + message;
        }
        function onAllFinished(successCount, total) {
            summarizeTimer.stop();
            rightPane.summarizing = false;
            rightPane.totalPapers = 0;
            rightPane.paperProgress = 0;
            rightPane.elapsedTime = 0;
            databaseManager.summaryModel.loadAll();
            numberPaper.text = "Paper Number: " + databaseManager.summaryProxyModel.rowCount();
            if (rightPane.statusBarLabel)
                rightPane.statusBarLabel.text = "Summarize done: " + successCount + "/" + total;
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
                onClicked: {
                    if (rightPane.selectedPapers.length === 0) {
                        console.log("No papers selected");
                        return;
                    }
                    rightPane.totalPapers = rightPane.selectedPapers.length;
                    rightPane.paperProgress = 0;
                    rightPane.elapsedTime = 0;
                    rightPane.summarizing = true;
                    summarizeTimer.start();
                    summaryController.startSummarize(rightPane.selectedPapers);
                    rightPane.selectedPapers = [];
                }
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
                }
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: "Generate Report from selection"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/list.svg"
                onClicked: console.log("Generate Report from", rightPane.selectedPapers.length, "selected papers")
            }

            Button {
                ToolTip.visible: hovered
                ToolTip.text: "Delete"
                icon.width: 24
                icon.height: 24
                Layout.fillWidth: true
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: rightPane.deleteSelected()
            }
        }

        // Progress section
        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            visible: rightPane.summarizing

            ProgressBar {
                id: summarizeProgress
                from: 0
                to: 100
                value: rightPane.totalPapers > 0 ? (rightPane.paperProgress / rightPane.totalPapers) * 100 : 0
                Layout.fillWidth: true
            }

            Label {
                text: "Summarizing " + rightPane.paperProgress + "/" + rightPane.totalPapers + " papers"
                Layout.fillWidth: true
            }

            Label {
                text: "Elapsed Time: " + (rightPane.elapsedTime / 10).toFixed(1) + "s"
                Layout.fillWidth: true
            }
        }

        Timer {
            id: summarizeTimer
            interval: 100
            running: false
            repeat: true
            onTriggered: {
                rightPane.elapsedTime += 1;
            }
        }

        // Controls
        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            CheckBox {
                id: selectAllCheckbox
                text: "Select All"
                checked: false
                onCheckedChanged: {
                    if (checked) {
                        for (var i = 0; i < databaseManager.summaryProxyModel.rowCount(); i++) {
                            var hash = databaseManager.summaryProxyModel.get(i, "paperHash");
                            rightPane.addSelectedPaper(hash);
                        }
                    } else {
                        rightPane.selectedPapers = [];
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }

            Label {
                id: numberPaper
                text: "Paper Number: " + rightPane.npapers
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
                    // TitleRole=259, DateRole=267, PagesNumRole=261, LlmModelRole=265
                    var roleMap = {
                        0: 259  // Title
                        ,
                        1: 267  // Date
                        ,
                        2: 261  // PagesNum
                        ,
                        3: 265   // LlmModel
                    };
                    databaseManager.summaryProxyModel.sortRole = roleMap[currentIndex] || 259;
                }
            }

            CheckBox {
                id: descendingCheckBox
                text: "Descending"
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

            model: databaseManager.summaryProxyModel
            spacing: 5

            delegate: Rectangle {
                required property string title
                required property string authors
                required property int pagesNum
                required property string llmodel
                required property string formattedDate
                required property string paperHash
                width: paperListView.width
                height: 120
                border.width: rightPane.activePaperHash === paperHash ? 4 : 5
                border.color: rightPane.activePaperHash === paperHash ? Colors.primary : Colors.surface1
                radius: 4
                color: mouseArea.containsMouse ? Colors.primary : Colors.surface0

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: rightPane.activePaperHash = paperHash
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 1
                    RowLayout {
                        CheckBox {
                            checked: rightPane.selectedPapers.includes(paperHash)
                            icon.height: 8
                            font.pixelSize: 10
                            onCheckedChanged: {
                                if (checked) {
                                    rightPane.addSelectedPaper(paperHash);
                                } else {
                                    rightPane.removeSelectedPaper(paperHash);
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
                        text: authors || "Please summarise for authors name"
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        opacity: 0.7
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            font.pixelSize: 12
                            text: pagesNum
                            font.family: Fonts.mono
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
}
