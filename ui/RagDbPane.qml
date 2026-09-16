pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: dbPane
    property var activeTheme: ({})
    property list<int> selectedQueryIds: []
    property int activeQueryId: 0
    property int nqueries: databaseManager.queryProxyModel.rowCount()

    function addSelectedQuery(id) {
        if (!selectedQueryIds.includes(id)) {
            selectedQueryIds = selectedQueryIds.concat([id]);
        }
    }

    function removeSelectedQuery(id) {
        var result = [];
        for (var i = 0; i < selectedQueryIds.length; i++) {
            if (selectedQueryIds[i] !== id) {
                result.push(selectedQueryIds[i]);
            }
        }
        selectedQueryIds = result;
    }

    function deleteSelected() {
        if (selectedQueryIds.length === 0) {
            console.log("No queries selected");
            return;
        }
        console.log("Deleting", selectedQueryIds.length, "queries");
        databaseManager.removeQueriesByIds(selectedQueryIds);
        selectedQueryIds = [];
    }

    border.width: 1
    radius: 5
    color: Colors.surface0
    opacity: 1.0



    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Query History")
                Layout.fillWidth: true
                font.pointSize: 16
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            CheckBox {
                id: selectAllCheckbox
                text: qsTr("Select All")
                checked: false
                onCheckedChanged: {
                    if (checked) {
                        for (var i = 0; i < databaseManager.queryProxyModel.rowCount(); i++) {
                            var id = databaseManager.queryProxyModel.get(i, "id");
                            dbPane.addSelectedQuery(id);
                        }
                    } else {
                        selectedQueryIds = [];
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ComboBox {
                id: sortComboBox
                model: [qsTr("Date"), qsTr("Model")]
                Layout.fillWidth: true
                onCurrentIndexChanged: {
                    // QueryTextRole=258, DateRole=260, LlmModelRole=261
                    var roleMap = {
                        0: 260  // Date
                        ,
                        1: 261  // LlmModel
                    };
                    databaseManager.queryProxyModel.sortRole = roleMap[currentIndex] || 260;
                }
            }

            CheckBox {
                id: descendingQueries
                text: qsTr("Descending")
                onCheckedChanged: databaseManager.queryProxyModel.sortDescending = checked
            }
            Label {
                id: numberQuery
                text: qsTr("Queries: %1").arg(dbPane.nqueries)
            }
        }

        ListView {
            id: queryView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.DragAndOvershootBounds
            Layout.minimumHeight: parent.height * 0.3
            focus: true
            highlight: Rectangle {
                color: "transparent"
            }

            Connections {
                target: dbPane
                function onActiveQueryIdChanged() {
                    for (var i = 0; i < databaseManager.queryProxyModel.rowCount(); i++) {
                        if (databaseManager.queryProxyModel.get(i, "id") === dbPane.activeQueryId) {
                            queryView.currentIndex = i;
                            break;
                        }
                    }
                }
            }
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < databaseManager.queryProxyModel.rowCount()) {
                    var qid = databaseManager.queryProxyModel.get(currentIndex, "id");
                    if (qid && qid !== dbPane.activeQueryId)
                        dbPane.activeQueryId = qid;
                }
            }

            ScrollBar.vertical: ScrollBar {
                id: queryScrollBar
                policy: ScrollBar.AsNeeded
                width: 10
                contentItem: Rectangle {
                    implicitWidth: 8
                    color: queryScrollBar.active ? Colors.overlay2 : Colors.surface2
                    radius: 4
                }
                background: Rectangle {
                    width: 8
                    color: "transparent"
                }
            }

            model: databaseManager.queryProxyModel
            spacing: 5

            delegate: Rectangle {
                required property int id
                required property string queryText
                required property string response
                required property string model
                required property string formattedDate
                id: queryCard
                width: queryView.width-10
                height: 80
                border.width: dbPane.activeQueryId === id ? 2 : 1
                border.color: dbPane.activeQueryId === id ? Colors.primary : Colors.surface1
                radius: 4
                color: mouseArea.containsMouse ? Colors.surface1 : Colors.surface0
                focus: dbPane.activeQueryId === id

                // Soft blurred halo behind the active query card (the glow)
                Loader {
                    id: queryGlow
                    anchors.fill: parent
                    sourceComponent: Colors.glowHalo
                    onLoaded: item.source = queryCard
                    visible: dbPane.activeQueryId === id
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        dbPane.activeQueryId = id;
                        queryView.forceActiveFocus();
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 1
                    RowLayout {
                        CheckBox {
                            checked: dbPane.selectedQueryIds.includes(id)
                            icon.height: 8
                            font.pixelSize: 10
                            onCheckedChanged: {
                                if (checked) {
                                    dbPane.addSelectedQuery(id);
                                } else {
                                    dbPane.removeSelectedQuery(id);
                                }
                            }
                        }
                        Label {
                            text: queryText || ""
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.bold: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: model || ""
                            font.family: Fonts.mono
                            font.pixelSize: 12
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: formattedDate || ""
                            font.family: Fonts.mono
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        RowLayout {
            id: buttonRowdbquery
            spacing: 10
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Delete")
                icon.width: 24
                icon.height: 24
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: dbPane.deleteSelected()
            }
        }
    }
}
