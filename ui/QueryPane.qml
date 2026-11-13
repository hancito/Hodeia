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

    // Context budget for the active backend; re-evaluated when the setting,
    // the backend or the model changes.
    readonly property var contextInfo: {
        configManager.activeBackend;
        configManager.activeModel;
        return queryController.contextInfo(contextSpinner.value);
    }

    // 16384 → "16k", 20000 → "19.5k"
    function formatTokens(v) {
        if (v < 1024)
            return String(v);
        const k = v / 1024;
        return (Number.isInteger(k) ? k : k.toFixed(1)) + "k";
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
                onAccepted: queryController.startQuery(queryLlm.text, contextSpinner.value)
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
                onClicked: queryPane.querying ? queryController.stopQuery() : queryController.startQuery(queryLlm.text, contextSpinner.value)
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
                    text: qsTr("Context")
                }
                // Prompt budget in tokens (0 = auto).  Type e.g. "24k",
                // "20000" or "auto".
                StyledSpinBox {
                    id: contextSpinner
                    Layout.preferredWidth: 170
                    from: 0
                    to: queryPane.contextInfo.maxTokens
                    stepSize: 2048
                    editable: true
                    value: configManager.queryContextTokens
                    onValueModified: configManager.queryContextTokens = value
                    validator: RegularExpressionValidator {
                        regularExpression: /^\s*(auto.*|\d+(\.\d+)?\s*k?)\s*$/i
                    }
                    textFromValue: function (v) {
                        return v === 0 ? qsTr("Auto (%1)").arg(queryPane.formatTokens(queryPane.contextInfo.autoTokens)) : queryPane.formatTokens(v);
                    }
                    valueFromText: function (text) {
                        const t = text.trim().toLowerCase();
                        if (t.startsWith("auto"))
                            return 0;
                        const n = parseFloat(t);
                        if (isNaN(n))
                            return value;
                        return Math.round(t.endsWith("k") ? n * 1024 : n);
                    }
                }
                // Where the local model will run with this budget.
                Label {
                    visible: !queryPane.contextInfo.remote && queryPane.contextInfo.placement !== ""
                    font.pixelSize: Style.fontSmall
                    font.bold: true
                    text: {
                        const p = queryPane.contextInfo.placement;
                        const where = p === "gpu" ? qsTr("GPU") : p === "split" ? qsTr("GPU + CPU") : qsTr("CPU (slow)");
                        return queryPane.contextInfo.truncated ? qsTr("%1 · truncated").arg(where) : where;
                    }
                    color: queryPane.contextInfo.truncated || queryPane.contextInfo.placement === "cpu" ? Colors.red : queryPane.contextInfo.placement === "split" ? Colors.primary : Colors.green
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
