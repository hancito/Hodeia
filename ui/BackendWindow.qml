pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: backendWindow
    width: 400
    height: 400
    title: qsTr("Backend Setup")
    // Key properties for modality
    modal: true
    focus: true
    ColumnLayout{
anchors.fill: parent
      RowLayout {
        Layout.fillWidth:  true
        Label {
            text: "Backend"
        }

        ComboBox {
            id: backendComboBox
            Layout.fillWidth: true
            model: ["Ollama","OpenAi Compatible"]

        }

      }

      RowLayout{
        Layout.fillWidth:  true
        Label {
          id: labApiUrl
            text: "API_URL"
        }

        TextField  {
            id: apiUrlValue
            Layout.fillWidth: true
        }

      }

      RowLayout:{
        Layout.fillWidth:  true
        Label {
            id: labApiKey
            text: "API_KEY"
        }

        TextField {
          id: apiKeyValue
            Layout.fillWidth: true
        }


      }

      Label {
          id: message
          text: "Please set your backend"
      }

RowLayout{
Layout.fillWidth:true
Button{
  id: checkApi
  text:"Check"
}
        standardButtons: Dialog.Ok | Dialog.Cancel
}


    }

}
