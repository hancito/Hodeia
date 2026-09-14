pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: backendWindow
    width: 520
    height: 600
    title: qsTr("Backend Management")
    anchors.centerIn: parent
    modal: true
    focus: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
        nameValue.text = "";
        urlValue.text = "";
        keyValue.text = "";
        editingIndex = -1;
        connectionVerified = false;
        modelCheckLabel.visible = false;
    }

    property int editingIndex: -1
    property bool connectionVerified: false

    // React to checkRemoteModels result
    Connections {
        target: helpers
        function onModelsChecked(success, models, error) {
            if (success) {
                modelCheckLabel.text = "Connected — " + models.length + " model(s) available";
                modelCheckLabel.visible = true;
                connectionVerified = true;
            } else {
                modelCheckLabel.text = "Connection failed: " + error;
                modelCheckLabel.visible = true;
                connectionVerified = false;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // --- Existing backends list ---
        GroupBox {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Backends"

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

                    Button {
                        text: "Edit"
                        visible: modelData.name !== "Llama.cpp"
                        onClicked: {
                            editingIndex = index;
                            nameValue.text = modelData.name || "";
                            urlValue.text = modelData.url || "";
                            keyValue.text = modelData.api_key || "";
                            connectionVerified = false;
                        }
                    }

                    Button {
                        text: "Remove"
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
        GroupBox {
            Layout.fillWidth: true
            title: editingIndex >= 0 ? "Edit Backend" : "Add Backend"

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Name"
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: nameValue
                        Layout.fillWidth: true
                        enabled: editingIndex < 0
                        placeholderText: "my-backend"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "URL"
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: urlValue
                        Layout.fillWidth: true
                        placeholderText: "http://localhost:11434/v1"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "API Key"
                        Layout.preferredWidth: 80
                    }
                    TextField {
                        id: keyValue
                        Layout.fillWidth: true
                        echoMode: TextField.Password
                        placeholderText: "API Key (empty if not needed)"
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Check Connection")
                    highlighted: false
                    onClicked: {
                        let n = nameValue.text.trim() || "Remote";
                        let u = urlValue.text.trim();
                        let k = keyValue.text.trim();
                        if (u === "") {
                            modelCheckLabel.text = "Please enter a URL";
                            modelCheckLabel.visible = true;
                            return;
                        }
                        helpers.checkRemoteModels(n, u, k);
                    }
                }

                Label {
                    id: modelCheckLabel
                    visible: false
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Button {
                    Layout.fillWidth: true
                    text: editingIndex >= 0 ? qsTr("Save Changes") : qsTr("Add Backend")
                    highlighted: true
                    visible: editingIndex >= 0 || connectionVerified
                    enabled: connectionVerified
                    onClicked: {
                        let n = nameValue.text.trim();
                        if (n === "")
                            return;
                        configManager.setBackend(n, urlValue.text.trim(), keyValue.text.trim());
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
