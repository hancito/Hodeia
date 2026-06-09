import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1140
    height: 757
    title: qsTr("Paperize")
    visible: true
    //icon.source: "qrc:/Ressources/paperize.png"

    menuBar: MyMenuBar {
        id: topNavigation
        // You can customize properties or add menus here
    }

    SplitView {
        anchors.top: topNavigation.bottom
        anchors.bottom: theFooter.top
        anchors.left: parent.left
        anchors.right: parent.right
        orientation: Qt.Horizontal
        
        LeftPane {
            id: leftPaneItem
            SplitView.preferredWidth: 400
            SplitView.maximumWidth: 400
            width: SplitView.preferredWidth
            height: parent.height
        }

        CentralPane {
            id: centralPaneItem
            SplitView.preferredWidth: 800
            SplitView.maximumWidth: 1000
            width: SplitView.preferredWidth
            height: parent.height
        }

        RightPane {
            id: rightPaneItem
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: 1000
            width: SplitView.preferredWidth
            height: parent.height
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