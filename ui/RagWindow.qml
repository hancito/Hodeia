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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing

        // Two modes: questions answered from the library (classic), or a
        // conversation with an agent that searches it itself (agentique).
        StyledTabBar {
            id: modeBar
            Layout.fillWidth: true
            StyledTabButton {
                text: qsTr("Questions")
            }
            StyledTabButton {
                text: qsTr("Conversation with an Agent")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: modeBar.currentIndex

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
