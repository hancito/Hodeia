import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects

ApplicationWindow {
    id: root
    width: 1920
    height: 1080
    title: qsTr("Paperize")
    visible: true
    //icon.source: "qrc:/Resources/paperize.png"

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
            id: mainSplitView
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 10
            handle: Item {
                implicitWidth: 10
            }

            LeftPane {
                id: leftPaneItem
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 400
            }

            CentralPane {
                id: centralPaneItem
                SplitView.minimumWidth: 600
            }

            RightPane {
                id: rightPaneItem
                SplitView.minimumWidth: 400
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
