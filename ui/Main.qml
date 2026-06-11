import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1920
    height: 1080
    title: qsTr("Paperize")
    visible: true
    //icon.source: "qrc:/Resources/paperize.png"
    property var secondaryWindow: null
    property var windowComponent: null

    Component.onCompleted: {
        // Load the QML file as a component
        windowComponent = Qt.createComponent("RagWindow.qml");
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
                onOpenRagWindow: {
                    // Check if the window instance already exists and is valid
                    if (root.secondaryWindow !== null) {
                        console.log("Window already open. Bringing to focus...");
                        root.secondaryWindow.raise();            // Brings the window to the top of the stack
                        root.secondaryWindow.requestActivate(); // Gives the window keyboard/mouse focus
                        return;
                    }

                    // 2. If it doesn't exist, create it
                    if (root.windowComponent.status === Component.Ready) {
                        let newWin = root.windowComponent.createObject(root);
                        if (newWin !== null) {
                            root.secondaryWindow = newWin; // Store the reference

                            // 3. Listen for the window closing so we can reset our reference to null
                            newWin.closed.connect(function () {
                                root.secondaryWindow = null;
                                console.log("Window closed. Reference cleared.");
                            });
                        }
                    } else {
                        console.log("Component not ready:", root.windowComponent.errorString());
                    }
                }
            }

            CentralPane {
                id: centralPaneItem
                SplitView.minimumWidth: 1000
            }

            RightPane {
                id: rightPaneItem
                SplitView.minimumWidth: 600
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