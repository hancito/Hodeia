pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ThemedWindow {
    id: ragwindow
    width: 1200
    height: 800
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
                SplitView.minimumWidth: 500
            }

            QueryPane {
                id: queryPaneItem
                SplitView.minimumWidth: 600
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
