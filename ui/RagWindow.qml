import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: ragwindow
    width: 1000
    height: 800
    title: qsTr("Paperize")
    visible: true
    //icon.source: "qrc:/Resources/paperize.png"
    // Signal to notify the main app that this window is closing
    signal closed()

    onClosing: {
        ragwindow.closed() // Fire our custom signal
        ragwindow.destroy() // Free memory
    }

    menuBar: MyMenuBar {
        id: topNavigation
        // You can customize properties or add menus here
    }

    GridLayout {
        anchors.fill: parent
        anchors.top: parent.top
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10

        SplitView {
            id: secondSplitView
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 8
            handle: Item {
                implicitWidth: 8
            }

            RagDbPane {
                id: dbPaneItem
                SplitView.minimumWidth: 400
            }

            QueryPane {
                id: queryPaneItem
                SplitView.minimumWidth: 500
            }

        }
    }

    // Status bar
    footer: ToolBar {
        id: theFooter
        height: 30
        Label {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            text: "Ready"
        }
    }
}
