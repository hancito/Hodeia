pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

StyledDialog {
    id: backendWindow
    width: 520
    height: 600
    title: qsTr("Backend Management")
    anchors.centerIn: parent
    focus: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
        nameValue.text = "";
        urlValue.text = "";
        keyValue.text = "";
        apiValue.currentIndex = 0;
        editingIndex = -1;
        connectionVerified = false;
        modelCheckLabel.visible = false;
    }

    property int editingIndex: -1
    property bool connectionVerified: false
    // Protocol of the backend being edited ("openai" | "anthropic" | "agent").
    // "agent": a coding agent (Claude Code, Qwen Code, Pi...) from the agent
    // profiles; the URL field may replace its command.
    readonly property string apiType: ["openai", "anthropic", "agent"][apiValue.currentIndex] || "openai"
    readonly property bool isAgent: apiType === "agent"
    // Read again at each opening: an agent may have been installed meanwhile.
    property var agentProfiles: helpers.agentProfiles()
    onAboutToShow: agentProfiles = helpers.agentProfiles()
    readonly property var selectedAgent: agentProfiles[agentValue.currentIndex] || null

    // React to checkRemoteModels result
    Connections {
        target: helpers
        function onModelsChecked(success, models, error) {
            if (success) {
                modelCheckLabel.text = qsTr("Connected — %1 model(s) available").arg(models.length);
                modelCheckLabel.visible = true;
                connectionVerified = true;
            } else {
                modelCheckLabel.text = qsTr("Connection failed: %1").arg(error);
                modelCheckLabel.visible = true;
                connectionVerified = false;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // --- Existing backends list ---
        StyledGroupBox {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Backends")

            ListView {
                id: backendListView
                anchors.fill: parent
                model: configManager.backendList
                clip: true

                delegate: RowLayout {
                    width: backendListView.width
                    required property var modelData
                    required property var index
                    Label {
                        text: modelData.name || ""
                        font.weight: Font.Bold
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    StyledButton {
                        text: qsTr("Edit")
                        visible: modelData.name !== "Llama.cpp"
                        onClicked: {
                            editingIndex = index;
                            nameValue.text = modelData.name || "";
                            urlValue.text = modelData.url || "";
                            keyValue.text = modelData.api_key || "";
                            apiValue.currentIndex = Math.max(0, ["openai", "anthropic", "agent"].indexOf(modelData.api));
                            agentValue.currentIndex = Math.max(0, backendWindow.agentProfiles.findIndex(p => p.id === modelData.agent));
                            connectionVerified = false;
                        }
                    }

                    StyledButton {
                        text: qsTr("Remove")
                        destructive: true
                        visible: modelData.name !== "Llama.cpp"
                        onClicked: {
                            configManager.removeBackend(modelData.name);
                            editingIndex = -1;
                            nameValue.clear();
                            urlValue.clear();
                            keyValue.clear();
                        }
                    }
                }
            }
        }

        // --- Add / Edit form ---
        StyledGroupBox {
            Layout.fillWidth: true
            title: editingIndex >= 0 ? qsTr("Edit Backend") : qsTr("Add Backend")

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Name")
                        Layout.preferredWidth: 80
                    }
                    StyledTextField {
                        id: nameValue
                        Layout.fillWidth: true
                        enabled: editingIndex < 0
                        placeholderText: qsTr("my-backend")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("API")
                        Layout.preferredWidth: 80
                    }
                    StyledComboBox {
                        id: apiValue
                        Layout.fillWidth: true
                        model: [qsTr("OpenAI-compatible"), qsTr("Anthropic"), qsTr("Agent (Claude Code, Codex, Qwen Code...)")]
                        onActivated: connectionVerified = false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backendWindow.isAgent
                    Label {
                        text: qsTr("Agent")
                        Layout.preferredWidth: 80
                    }
                    StyledComboBox {
                        id: agentValue
                        Layout.fillWidth: true
                        model: backendWindow.agentProfiles.map(p => {
                            if (p.missingRuntimes)
                                return qsTr("%1 (needs %2)").arg(p.name).arg(p.missingRuntimes);
                            if (!p.installed)
                                return qsTr("%1 (not installed)").arg(p.name);
                            return p.verified ? p.name : qsTr("%1 (not tested)").arg(p.name);
                        })
                        onActivated: connectionVerified = false
                    }
                }

                // How to get an agent that cannot start here.
                Label {
                    Layout.fillWidth: true
                    visible: backendWindow.isAgent && backendWindow.selectedAgent !== null
                             && (!backendWindow.selectedAgent.installed || backendWindow.selectedAgent.missingRuntimes !== "")
                    wrapMode: Text.WordWrap
                    color: Colors.red
                    font.pixelSize: Style.fontSmall
                    text: {
                        const a = backendWindow.selectedAgent;
                        if (!a)
                            return "";
                        let s = a.missingRuntimes
                                ? qsTr("%1 needs %2, which is not on this computer.").arg(a.name).arg(a.missingRuntimes)
                                : qsTr("%1 is not installed on this computer (or not in the PATH).").arg(a.name);
                        if (a.install)
                            s += " " + qsTr("Install it with: %1").arg(a.install);
                        return s;
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: backendWindow.isAgent ? qsTr("Command") : qsTr("URL")
                        Layout.preferredWidth: 80
                    }
                    StyledTextField {
                        id: urlValue
                        Layout.fillWidth: true
                        placeholderText: backendWindow.isAgent
                                         ? (backendWindow.selectedAgent ? backendWindow.selectedAgent.command : "")
                                         : backendWindow.apiType === "anthropic"
                                           ? "https://api.anthropic.com/v1"
                                           : "http://localhost:11434/v1"
                    }
                }

                // The agent uses its own login (the user's account with it).
                Label {
                    Layout.fillWidth: true
                    visible: backendWindow.isAgent
                    wrapMode: Text.WordWrap
                    color: Colors.overlay1
                    font.pixelSize: Style.fontSmall
                    text: qsTr("Uses the agent installed on this computer, with your own login to it. Leave the command empty to use the usual one.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !backendWindow.isAgent
                    Label {
                        text: qsTr("API Key")
                        Layout.preferredWidth: 80
                    }
                    StyledTextField {
                        id: keyValue
                        Layout.fillWidth: true
                        echoMode: TextField.Password
                        placeholderText: qsTr("API Key (empty if not needed)")
                    }
                }

                StyledButton {
                    Layout.fillWidth: true
                    text: qsTr("Check Connection")
                    highlighted: false
                    onClicked: {
                        let n = nameValue.text.trim() || "Remote";
                        let u = urlValue.text.trim();
                        let k = keyValue.text.trim();
                        if (u === "" && !backendWindow.isAgent) {
                            modelCheckLabel.text = qsTr("Please enter a URL");
                            modelCheckLabel.visible = true;
                            return;
                        }
                        if (backendWindow.isAgent) {
                            if (!backendWindow.selectedAgent)
                                return;
                            modelCheckLabel.text = qsTr("Starting the agent...");
                            modelCheckLabel.visible = true;
                            helpers.checkAgentModels(nameValue.text.trim() || backendWindow.selectedAgent.name,
                                                     backendWindow.selectedAgent.id, u);
                            return;
                        }
                        helpers.checkRemoteModels(n, u, k, backendWindow.apiType);
                    }
                }

                Label {
                    id: modelCheckLabel
                    visible: false
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                StyledButton {
                    Layout.fillWidth: true
                    text: editingIndex >= 0 ? qsTr("Save Changes") : qsTr("Add Backend")
                    highlighted: true
                    visible: editingIndex >= 0 || connectionVerified
                    enabled: connectionVerified
                    onClicked: {
                        let n = nameValue.text.trim();
                        if (n === "")
                            return;
                        if (backendWindow.isAgent && backendWindow.selectedAgent)
                            configManager.setAgentBackend(n, backendWindow.selectedAgent.id, urlValue.text.trim());
                        else
                            configManager.setBackend(n, urlValue.text.trim(), keyValue.text.trim(),
                                                     backendWindow.apiType);
                        editingIndex = -1;
                        nameValue.clear();
                        urlValue.clear();
                        keyValue.clear();
                        modelCheckLabel.visible = false;
                    }
                }
            }
        }
    }
}
