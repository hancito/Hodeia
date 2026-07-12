pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: queryPane
    property var activeTheme: ({})
    property int activeQueryId: 0
    property string lastResponse: ""

    // Live lookup of the active query from the model.
    readonly property string activeQueryText: {
        if (activeQueryId === 0)
            return "";
        for (var i = 0; i < databaseManager.queryModel.rowCount(); i++) {
            var rowId = databaseManager.queryModel.dataAt(i, "id");
            if (rowId === activeQueryId)
                return databaseManager.queryModel.dataAt(i, "queryText");
        }
        return "";
    }
    readonly property string activeQueryResponse: {
        if (activeQueryId === 0)
            return "";
        for (var i = 0; i < databaseManager.queryModel.rowCount(); i++) {
            var rowId = databaseManager.queryModel.dataAt(i, "id");
            if (rowId === activeQueryId)
                return databaseManager.queryModel.dataAt(i, "response");
        }
        return "";
    }

    border.width: 1
    radius: 5
    color: Colors.surface0
    opacity: 1.0

    Connections {
        target: queryController
        function onQueryFinished(response, success, error) {
            if (success) {
                queryPane.lastResponse = response;
            } else {
                queryPane.lastResponse = "Error: " + error;
            }
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
                placeholderText: "Query your library"
                text: queryPane.activeQueryText
            }

            Button {
                ToolTip.visible: hovered
                text: "Query"
                ToolTip.text: "Query"
                icon.width: 24
                icon.height: 24
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: queryController.startQuery(queryLlm.text, maxPagesSpinner.value)
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
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.WordWrap
                    // Prefer the active query response; fall back to lastResponse from a live query.
                    text: queryPane.activeQueryResponse !== "" ? queryPane.activeQueryResponse : queryPane.lastResponse !== "" ? queryPane.lastResponse : "Ask a question about your paper library..."
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
