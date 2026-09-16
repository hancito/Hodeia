pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: ragwindow
    width: 1200
    height: 800
    title: qsTr("Hodeia")
    visible: true
    font.family: Fonts.ui
    //icon.source: "qrc:/Resources/hodeia.png"
    // Signal to notify the main app that this window is closing
    readonly property var activeTheme: themeManager.activeTheme || {}
    signal closed

    onClosing: {
        // Stop any in-flight query before closing
        queryController.stopQuery();
        ragwindow.closed(); // Fire our custom signal
        ragwindow.destroy(); // Free memory
    }

    // Global palette applied to all QtQuick.Controls
    palette.base: Colors.crust
    palette.alternateBase: Colors.base
    palette.window: Colors.base
    palette.windowText: Colors.text
    palette.button: Colors.base
    palette.buttonText: Colors.text
    palette.brightText: Colors.mantle
    palette.text: Colors.text
    palette.placeholderText: Colors.overlay1
    palette.light: Colors.crust
    palette.midlight: Colors.mantle
    palette.mid: Colors.surface0
    palette.dark: Colors.surface1
    palette.shadow: Colors.overlay0
    palette.highlight: Colors.primary
    palette.highlightedText: Colors.primary === "#cba6f7" ? Colors.mantle : Colors.text

    // Inactive
    palette.inactive.highlight: Colors.surface1
    palette.inactive.link: Colors.surface1
    palette.inactive.linkVisited: Colors.surface1

    // Disabled
    palette.disabled.windowText: Colors.overlay1
    palette.disabled.base: Colors.base
    palette.disabled.alternateBase: Colors.base
    palette.disabled.text: Colors.overlay1
    palette.disabled.placeholderText: Colors.overlay1
    palette.disabled.button: Colors.base
    palette.disabled.buttonText: Colors.overlay1
    palette.disabled.brightText: Colors.mantle
    palette.disabled.highlight: Colors.surface2
    palette.disabled.highlightedText: Colors.surface0
    palette.disabled.link: Colors.surface0
    palette.disabled.linkVisited: Colors.surface0

    // Apply theme to the entire window content
    background: Rectangle {
        color: Colors.base || Colors.background || "#1e1e1e"
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
                SplitView.minimumWidth: 600
                activeQueryId: dbPaneItem.activeQueryId
            }
        }
    }

    // Status bar
    footer: ToolBar {
        id: theFooter
        height: 30
        Label {
            anchors.fill: parent
            verticalAlignment: Text.AlignLeft
            horizontalAlignment: Text.AlignLeft
            text: qsTr("Ready")
        }
    }
}
