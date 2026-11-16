pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ThemedWindow {
    id: ragwindow
    preferredWidth: 1200
    preferredHeight: 800
    title: qsTr("Hodeia")
    visible: true
    //icon.source: "qrc:/Resources/hodeia.png"
    // Signal to notify the main app that this window is closing
    signal closed

    onClosing: {
        // Stop any in-flight query before closing
        queryController.stopQuery();
        ragwindow.closed(); // Fire our custom signal
        ragwindow.destroy(); // Free memory
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin

        SplitView {
            id: secondSplitView
            Layout.fillHeight: true
            Layout.fillWidth: true
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
