pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ThemedWindow {
    id: ragwindow
    preferredWidth: 1200
    preferredHeight: 800
    title: qsTr("HodeIA")
    visible: true
    //icon.source: "qrc:/Resources/hodeia.png"
    // Signal to notify the main app that this window is closing
    signal closed

    onClosing: {
        // Stop any in-flight query before closing
        queryController.stopQuery();
        agentChat.closeAgent();
        ragwindow.closed(); // Fire our custom signal
        ragwindow.destroy(); // Free memory
    }

    // Mode agentique: only with an agent backend (Claude Code...); back to
    // the classic mode as soon as the active backend is not one.
    readonly property bool agentMode: modeSwitch.checked && agentChat.agentAvailable

    // Two modes, two panes each: the query history and the answer (classic),
    // or the conversations and the thread with the agent (agentique).
    header: ToolBar {
        leftPadding: Style.paneMargin
        rightPadding: Style.paneMargin
        topPadding: 4
        bottomPadding: 4
        background: Rectangle {
            color: Colors.mantle
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.surface0
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 10
            StyledSwitch {
                id: modeSwitch
                text: qsTr("Agent mode")
                // The last choice, kept in the settings; shown on only while
                // an agent backend is active.
                checked: configManager.agentMode && agentChat.agentAvailable
                onToggled: configManager.agentMode = checked
                enabled: agentChat.agentAvailable && !agentChat.busy && !queryPaneItem.querying
                toolTip: agentChat.agentAvailable
                         ? qsTr("Converse with %1: it searches your library itself and suggests ways to take the research further.").arg(configManager.activeBackend)
                         : qsTr("Choose an agent backend (Claude Code, Qwen Code...) in the toolbar of the main window to use the agent mode.")
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Colors.overlay1
                text: configManager.activeBackend + (configManager.activeModel ? " · " + configManager.activeModel : "")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ragwindow.agentMode ? 1 : 0

            SplitView {
                id: secondSplitView
                handle: SplitHandleBar {}

                RagDbPane {
                    id: dbPaneItem
                    // Proportional width so both panes fit with display scaling.
                    SplitView.preferredWidth: secondSplitView.width * 0.4
                    SplitView.minimumWidth: 260
                }

                QueryPane {
                    id: queryPaneItem
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 360
                    activeQueryId: dbPaneItem.activeQueryId
                }
            }

            SplitView {
                id: agentSplitView
                handle: SplitHandleBar {}

                ConversationListPane {
                    SplitView.preferredWidth: agentSplitView.width * 0.3
                    SplitView.minimumWidth: 220
                }

                ConversationPane {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 360
                }
            }
        }
    }

    // Status bar
    footer: StatusBar {
        id: footerLabel
        text: qsTr("Ready")
    }

    // User-facing information and errors for the query window.
    MessageBox {
        id: messageBox
    }

    // The switch follows the setting and the backend (a toggle breaks its
    // binding): off without an agent backend, the user's choice with one.
    Connections {
        target: agentChat
        function onAgentBackendsChanged() {
            modeSwitch.checked = configManager.agentMode && agentChat.agentAvailable;
        }
    }

    // Problems of the conversations outside a message (database...).
    Connections {
        target: agentChat
        function onErrorOccurred(message) {
            messageBox.showError(qsTr("Conversation"), message);
        }
    }

    // Set by Stop: the "cancelled" failure that follows is not an error.
    property bool queryWasStopped: false

    // ── Status bar and message windows from the query controller ──
    Connections {
        target: queryController
        function onQueryStarted() {
            ragwindow.queryWasStopped = false;
            footerLabel.text = qsTr("Querying...");
        }
        function onQueryFinished(response, success, error) {
            if (success) {
                footerLabel.text = qsTr("Query completed");
                // Answer shown, but not stored in the history.
                if (error !== "")
                    messageBox.showError(qsTr("Query Not Saved"), error);
            } else if (ragwindow.queryWasStopped) {
                footerLabel.text = qsTr("Query stopped");
            } else {
                footerLabel.text = qsTr("Query failed: %1").arg(error);
                messageBox.showError(qsTr("Query Failed"), error);
            }
        }
        function onQueryStopped() {
            ragwindow.queryWasStopped = true;
            footerLabel.text = qsTr("Query stopped");
        }
    }
}
