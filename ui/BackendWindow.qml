pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: backendWindow
    width: 400
    height: 400
    title: qsTr("Backend Setup")
    anchors.centerIn: parent
    // Key properties for modality
    modal: true
    focus: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Name"
            }

            TextField {
                id: nameValue
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Type"
            }

            ComboBox {
                id: backendComboBox
                Layout.fillWidth: true
                model: ["Ollama", "OpenAi Compatible"]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                id: labApiUrl
                text: "API_URL"
            }

            TextField {
                id: apiUrlValue
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                id: labApiKey
                text: "API_KEY"
            }

            TextField {
                id: apiKeyValue
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            Label {
                id: message
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter // Options: AlignLeft, AlignRight, AlignHCenter, AlignJustify
                verticalAlignment: Text.AlignVCenter   // Options: AlignTop, AlignBottom, AlignVCenter
                text: "Please set your backend"
            }

            Button {
                id: checkApi
                Layout.fillWidth: true
                text: "Check"
            }
        }
    }
}
