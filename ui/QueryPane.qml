pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: queryPane
    property var activeTheme: ({})
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
    radius: 5
    color: Colors.surface0
    opacity: 1.0

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
        anchors.margins: 5
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: queryLlm
                Layout.fillWidth: true
                placeholderText: qsTr("Query your library")
                text: queryPane.activeQueryText
                enabled: !queryPane.querying
                onAccepted: queryController.startQuery(queryLlm.text, maxPagesSpinner.value)
            }

            BusyIndicator {
                running: queryPane.querying
                visible: queryPane.querying
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                palette.dark: Colors.primary
            }

            Button {
                ToolTip.visible: hovered && !queryPane.querying
                text: queryPane.querying ? qsTr("Stop") : qsTr("Query")
                ToolTip.text: queryPane.querying ? qsTr("Stop") : qsTr("Query")
                icon.width: 24
                icon.height: 24
                icon.source: queryPane.querying ? "qrc:/Resources/circle-xmark.svg" : "qrc:/Resources/ai-svgrepo-com.svg"
                highlighted: queryPane.querying
                onClicked: queryPane.querying ? queryController.stopQuery() : queryController.startQuery(queryLlm.text, maxPagesSpinner.value)
            }
        }

        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                TextArea {
                    id: textEditor
                    readOnly: true
                    font.family: Fonts.text
                    font.pointSize: 13
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
                SpinBox {
                    id: maxPagesSpinner
                    from: 10
                    to: 300
                    value: 50
                    stepSize: 1
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
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
