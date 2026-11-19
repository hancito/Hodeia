pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Mode agentique: the thread of the open conversation and the message box.
// The agent searches the library itself (HodeIA's MCP tools); its answers
// come with author-year citations, a Sources section, and research leads.
Rectangle {
    id: conversationPane

    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge
    color: Colors.surface0

    readonly property var backends: agentChat.agentBackends
    readonly property bool newConversation: agentChat.currentId === 0

    function sendMessage() {
        const text = input.text.trim();
        if (text === "" || agentChat.busy)
            return;
        // A new conversation takes the agent and model chosen above.
        if (conversationPane.newConversation && agentCombo.currentIndex >= 0)
            agentChat.newConversation(backends[agentCombo.currentIndex].name, modelField.text.trim());
        agentChat.send(text);
        input.text = "";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing

        // ── Agent and model ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Agent")
            }
            StyledComboBox {
                id: agentCombo
                Layout.fillWidth: true
                visible: conversationPane.newConversation
                enabled: !agentChat.busy
                model: conversationPane.backends.map(b => b.name)
                currentIndex: Math.max(0, conversationPane.backends.findIndex(b => b.name === agentChat.backend))
                onActivated: modelField.text = conversationPane.backends[currentIndex].model || ""
            }
            StyledTextField {
                id: modelField
                Layout.preferredWidth: 160
                visible: conversationPane.newConversation
                enabled: !agentChat.busy
                placeholderText: qsTr("Model (default)")
                text: agentChat.model
            }
            Label {
                Layout.fillWidth: true
                visible: !conversationPane.newConversation
                elide: Text.ElideRight
                color: Colors.overlay1
                text: agentChat.backend + (agentChat.model ? " · " + agentChat.model : "")
            }
        }

        Label {
            Layout.fillWidth: true
            visible: conversationPane.backends.length === 0
            wrapMode: Text.WordWrap
            color: Colors.red
            text: qsTr("No agent can search the library yet: add Claude Code (or another agent that supports MCP) in the Backends window.")
        }

        // ── The thread ──
        ListView {
            id: thread
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: agentChat.messages
            ScrollBar.vertical: StyledScrollBar {}
            onCountChanged: Qt.callLater(thread.positionViewAtEnd)

            delegate: Rectangle {
                id: bubble
                required property var modelData
                readonly property bool isUser: modelData.role === "user"
                readonly property bool isError: modelData.role === "error"

                width: ListView.view.width - 12
                x: isUser ? 12 : 0
                height: body.implicitHeight + 16
                radius: Style.radiusSmall
                color: isUser ? Colors.surface1 : isError ? "transparent" : Colors.base
                border.width: isError ? 1 : 0
                border.color: Colors.red

                TextEdit {
                    id: body
                    anchors.fill: parent
                    anchors.margins: 8
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: bubble.isError ? Colors.red : Colors.text
                    font.family: Fonts.text
                    font.pixelSize: Style.fontReading
                    font.hintingPreference: Font.PreferNoHinting
                    textFormat: bubble.isUser || bubble.isError ? TextEdit.PlainText : TextEdit.RichText
                    text: bubble.isUser || bubble.isError ? bubble.modelData.text : helpers.markdownToHtml(bubble.modelData.text)
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: thread.count === 0
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Colors.overlay1
            text: qsTr("Ask a question: the agent searches your library, answers with citations and suggests ways to take the research further.")
        }

        // ── What the agent is doing ──
        RowLayout {
            Layout.fillWidth: true
            visible: agentChat.busy
            StyledBusyIndicator {
                running: agentChat.busy
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Colors.overlay1
                text: agentChat.status
            }
        }

        // ── Message box ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(140, Math.max(44, input.implicitHeight + 8))
                TextArea {
                    id: input
                    wrapMode: TextEdit.Wrap
                    font.family: Fonts.text
                    font.pixelSize: Style.fontReading
                    color: Colors.text
                    placeholderText: qsTr("Message the agent (Enter to send, Shift+Enter for a new line)")
                    placeholderTextColor: Colors.overlay1
                    enabled: conversationPane.backends.length > 0
                    background: Rectangle {
                        radius: Style.radiusSmall
                        color: Colors.base
                        border.width: 1
                        border.color: input.activeFocus ? Colors.primary : Colors.surface1
                    }
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier) {
                            event.accepted = false;
                            return;
                        }
                        conversationPane.sendMessage();
                    }
                }
            }
            StyledButton {
                text: agentChat.busy ? qsTr("Stop") : qsTr("Send")
                highlighted: true
                destructive: agentChat.busy
                enabled: agentChat.busy || (input.text.trim() !== "" && conversationPane.backends.length > 0)
                onClicked: agentChat.busy ? agentChat.stop() : conversationPane.sendMessage()
            }
        }
    }
}
