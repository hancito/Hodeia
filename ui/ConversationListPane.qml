pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Mode agentique: the conversations with the agent, the most recent first.
// Opening one shows it in ConversationPane; "New conversation" starts an
// empty one (created by its first message).
Rectangle {
    id: listPane

    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge
    color: Colors.surface0

    readonly property var current: {
        for (const c of agentChat.conversations)
            if (c.id === agentChat.currentId)
                return c;
        return null;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing

        Label {
            text: qsTr("Conversations")
            Layout.fillWidth: true
            font.pixelSize: Style.fontTitle
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
        }

        StyledButton {
            Layout.fillWidth: true
            text: qsTr("New Conversation")
            emphasized: true
            enabled: !agentChat.busy
            onClicked: agentChat.newConversation(configManager.activeBackend, configManager.activeModel)
        }

        ListView {
            id: conversationView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: agentChat.conversations
            ScrollBar.vertical: StyledScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                readonly property bool isCurrent: modelData.id === agentChat.currentId

                width: ListView.view.width
                enabled: !agentChat.busy || isCurrent
                background: Rectangle {
                    radius: Style.radiusSmall
                    color: row.isCurrent ? Colors.surface1 : row.hovered ? Colors.base : "transparent"
                }
                onClicked: agentChat.openConversation(modelData.id)

                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.title || qsTr("(untitled)")
                        font.bold: row.isCurrent
                        color: Colors.text
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        font.pixelSize: Style.fontSmall
                        color: Colors.overlay1
                        elide: Text.ElideRight
                        text: {
                            const m = row.modelData;
                            let s = m.backend + (m.model ? " · " + m.model : "");
                            s += " · " + m.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat);
                            return s;
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: conversationView.count === 0
            wrapMode: Text.WordWrap
            color: Colors.overlay1
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No conversation yet.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledButton {
                Layout.fillWidth: true
                text: qsTr("Rename...")
                enabled: listPane.current !== null && !agentChat.busy
                onClicked: {
                    renameField.text = listPane.current.title;
                    renameDialog.open();
                }
            }
            StyledButton {
                Layout.fillWidth: true
                text: qsTr("Delete...")
                destructive: true
                enabled: listPane.current !== null && !agentChat.busy
                onClicked: deleteDialog.open()
            }
        }
    }

    StyledDialog {
        id: renameDialog
        anchors.centerIn: parent
        width: Math.min(420, listPane.width)
        title: qsTr("Rename the Conversation")
        standardButtons: Dialog.Ok | Dialog.Cancel
        StyledTextField {
            id: renameField
            width: renameDialog.availableWidth
            onAccepted: renameDialog.accept()
        }
        onAccepted: {
            const err = agentChat.rename(agentChat.currentId, renameField.text);
            if (err !== "")
                messageBox.showError(qsTr("Rename Failed"), err);
        }
    }

    StyledDialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: Math.min(420, listPane.width)
        title: qsTr("Delete the Conversation")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: deleteDialog.availableWidth
            wrapMode: Text.WordWrap
            text: listPane.current ? qsTr("Delete \"%1\" and its messages?").arg(listPane.current.title) : ""
        }
        onAccepted: {
            const err = agentChat.remove([agentChat.currentId]);
            if (err !== "")
                messageBox.showError(qsTr("Deletion Failed"), err);
        }
    }
}
