pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: backendWindow
    width: 520
    height: 480
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
    }

    property int editingIndex: -1

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

                delegate: RowLayout {
                    width: backendListView.width
                    property int idx: index

                    Label {
                        text: modelData.name || ""
                        font.weight: Font.Bold
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "Edit"
                        onClicked: {
                            editingIndex = idx;
                            nameValue.text = modelData.name || "";
                            urlValue.text = modelData.url || "";
                            keyValue.text = modelData.api_key || "";
                        }
                    }

                    Button {
                        text: "Remove"
                        enabled: modelData.name !== "llamacpp"
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
                        placeholderText: "optional"
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: editingIndex >= 0 ? qsTr("Save Changes") : qsTr("Add Backend")
                    highlighted: true
                    onClicked: {
                        let n = nameValue.text.trim();
                        if (n === "")
                            return;
                        configManager.setBackend(n, urlValue.text.trim(), keyValue.text.trim());
                        editingIndex = -1;
                        nameValue.clear();
                        urlValue.clear();
                        keyValue.clear();
                    }
                }
            }
        }
    }
}
