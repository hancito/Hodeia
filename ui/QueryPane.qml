pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: queryPane
    property int activeQueryId: 0
    property string lastResponse: ""
    property bool querying: false

    // Live lookup of the active query from the proxy model.
    readonly property string activeQueryText: {
        if (activeQueryId === 0)
            return "";
        for (var i = 0; i < databaseManager.queryProxyModel.rowCount(); i++) {
            var rowId = databaseManager.queryProxyModel.get(i, "id");
            if (rowId === activeQueryId)
                return databaseManager.queryProxyModel.get(i, "queryText");
        }
        return "";
    }
    readonly property string activeQueryResponse: {
        if (activeQueryId === 0)
            return "";
        for (var i = 0; i < databaseManager.queryProxyModel.rowCount(); i++) {
            var rowId = databaseManager.queryProxyModel.get(i, "id");
            if (rowId === activeQueryId)
                return databaseManager.queryProxyModel.get(i, "response");
        }
        return "";
    }

    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge
    color: Colors.surface0

    Connections {
        target: queryController
        function onQueryStarted() {
            queryPane.querying = true;
        }
        function onQueryFinished(response, success, error) {
            queryPane.querying = false;
            if (success) {
                queryPane.lastResponse = response;
            } else {
                queryPane.lastResponse = qsTr("Error: %1").arg(error);
            }
        }
        function onQueryStopped() {
            queryPane.querying = false;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing
        RowLayout {
            Layout.fillWidth: true
            StyledTextField {
                id: queryLlm
                Layout.fillWidth: true
                placeholderText: qsTr("Query your library")
                text: queryPane.activeQueryText
                enabled: !queryPane.querying
                onAccepted: queryController.startQuery(queryLlm.text, maxPagesSpinner.value)
            }

            StyledBusyIndicator {
                running: queryPane.querying
                visible: queryPane.querying
            }

            StyledButton {
                text: queryPane.querying ? qsTr("Stop") : qsTr("Query")
                toolTip: queryPane.querying ? "" : qsTr("Query")
                icon.width: 24
                icon.height: 24
                icon.source: queryPane.querying ? "qrc:/Resources/circle-xmark.svg" : "qrc:/Resources/ai-svgrepo-com.svg"
                highlighted: true
                destructive: queryPane.querying
                onClicked: queryPane.querying ? queryController.stopQuery() : queryController.startQuery(queryLlm.text, maxPagesSpinner.value)
            }
        }

        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: answerScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: StyledScrollBar {
                    parent: answerScroll
                    x: answerScroll.width - width
                    y: answerScroll.topPadding
                    height: answerScroll.availableHeight
                }
                TextArea {
                    id: textEditor
                    readOnly: true
                    font.family: Fonts.text
                    font.pixelSize: Style.fontReading
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.WordWrap
                    // Prefer the active query response; fall back to lastResponse from a live query.
                    text: queryPane.activeQueryResponse !== "" ? queryPane.activeQueryResponse : queryPane.lastResponse !== "" ? queryPane.lastResponse : qsTr("Ask a question about your paper library...")
                }
            }

            RowLayout {
                id: buttonRowquery
                spacing: 10
                Layout.fillWidth: true
                Label {
                    text: qsTr("Max Pages")
                }
                StyledSpinBox {
                    id: maxPagesSpinner
                    from: 10
                    to: 300
                    value: 50
                    stepSize: 1
                }

                Item {
                    Layout.fillWidth: true
                }

                StyledButton {
                    text: qsTr("Export")
                    icon.width: 24
                    icon.height: 24
                    icon.source: "qrc:/Resources/share-square.svg"
                    onClicked: console.log("Export Text")
                }
            }
        }
    }
}
