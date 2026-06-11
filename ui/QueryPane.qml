import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: queryPane
    border.color: "black"
    border.width: 1
    radius: 10
    color: "#FFFFFF"
    opacity: 0.5
    // gradient: Colors.invertedGreyBorder

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        RowLayout {
            TextField {
                id: queryLlm
                Layout.fillWidth: true
                placeholderText: "Query your library"
                //clearButtonEnabled: true
            }
            Button {
                text: "Query"
                ToolTip.text: "Query"
                icon.width: 24
                icon.height: 24
                height: queryLlm.height
                icon.source: "qrc:/Resources/ai-svgrepo-com.svg"
                onClicked: console.log("Query")
            }
        }

        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                TextArea {
                    id: textEditor
                    readOnly: true
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.WordWrap
                    text: "# TextArea Markdown\n\n" + "1. This is a scrollable area.\n" + "2. You can select this text.\n" + "3. **Bold** and *italics* work perfectly"
                }
            }

            RowLayout {
                id: buttonRowquery
                spacing: 10
                Layout.fillWidth: true
                Label {
                    text: qsTr("Context Window")
                }
                SpinBox {
                    id: contextRagWindowSpinner
                    from: 1000
                    to: 256000
                    value: 4000
                    stepSize: 100
                }

                Label {
                    text: qsTr("Max Pages")
                }
                SpinBox {
                    id: maxPagesSpinner
                    from: 10
                    to: 300
                    value: 50
                    stepSize: 1
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Export")
                    icon.width: 24
                    icon.height: 24
                    icon.source: "qrc:/Resources/share-square.svg"
                    onClicked: console.log("Export Text")
                }
            }
        }
    }
}
