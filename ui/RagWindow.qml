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

    // ── Status bar updates from query controller ──
    Connections {
        target: queryController
        function onQueryStarted() {
            footerLabel.text = qsTr("Querying...");
        }
        function onQueryFinished(response, success, error) {
            if (success)
                footerLabel.text = qsTr("Query completed");
            else
                footerLabel.text = qsTr("Query failed: %1").arg(error);
        }
        function onQueryStopped() {
            footerLabel.text = qsTr("Query stopped");
        }
    }
}
