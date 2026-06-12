pragma ComponentBehavior: Bound
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

    readonly property var activeTheme: themeManager.activeTheme || {}

    Component.onCompleted: {
        // Load the QML file as a component
        windowComponent = Qt.createComponent("RagWindow.qml");

        // Apply saved theme index from config
        if (configManager.activeThemeIndex !== undefined && configManager.activeThemeIndex >= 0) {
            themeManager.setThemeIndex(configManager.activeThemeIndex);
        }
    }
    menuBar: MyMenuBar {
        id: topNavigation
        // You can customize properties or add menus here
    }

    // Apply theme to the entire window content
    background: Rectangle {
        color: root.activeTheme.base ||  root.activeTheme.background || "#1e1e1e"
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
            handle: Rectangle {
                implicitWidth: 10
                color: "transparent"
                opacity: 0
            }

            LeftPane {
                id: leftPaneItem
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 400
                activeTheme: root.activeTheme
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
                activeTheme: root.activeTheme
            }

            RightPane {
                id: rightPaneItem
                SplitView.minimumWidth: 600
                activeTheme: root.activeTheme
            }
        }
    }

    // Status bar
    footer: ToolBar {
        id: theFooter
        height: 30
        background: Rectangle {
            color:  root.activeTheme.surface0 ||  root.activeTheme.mantle || "#2d2d2d"
        }
        Label {
            anchors.fill: parent
            verticalAlignment: Text.AlignLeft
            horizontalAlignment: Text.AlignLeft
                        text: "Ready"
            color:  root.activeTheme.text || "#ffffff"
        }
    }

}

