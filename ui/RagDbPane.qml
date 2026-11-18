pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: dbPane
    property list<int> selectedQueryIds: []
    property int activeQueryId: 0
    property int nqueries: databaseManager.queryProxyModel.count

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

    function showNoSelection() {
        messageBox.showInfo(qsTr("No Queries Selected"), qsTr("Select at least one query first."));
    }

    function deleteSelected() {
        if (selectedQueryIds.length === 0) {
            showNoSelection();
            return;
        }
        console.log("Deleting", selectedQueryIds.length, "queries");
        const activeDeleted = selectedQueryIds.includes(activeQueryId);
        const err = databaseManager.removeQueriesByIds(selectedQueryIds);
        selectedQueryIds = [];
        if (err !== "")
            messageBox.showError(qsTr("Deletion Incomplete"), err);
        // The active query is gone: follow the list's new current row (its
        // index may not change, so onCurrentIndexChanged won't fire), or
        // clear the selection when the list is empty.
        if (activeDeleted) {
            const i = queryView.currentIndex;
            activeQueryId = i >= 0 && i < databaseManager.queryProxyModel.rowCount() ? databaseManager.queryProxyModel.get(i, "id") : 0;
        }
    }

    border.width: 1
    border.color: Colors.surface1
    radius: Style.radiusLarge
    color: Colors.surface0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.paneMargin
        spacing: Style.spacing
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Query History")
                Layout.fillWidth: true
                font.pixelSize: Style.fontTitle
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true

            StyledCheckBox {
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
            StyledComboBox {
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

            StyledCheckBox {
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

            ScrollBar.vertical: StyledScrollBar {}

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
                radius: Style.radiusSmall
                color: mouseArea.containsMouse ? Colors.surface1 : Colors.surface0
                focus: dbPane.activeQueryId === id


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
                        StyledCheckBox {
                            checked: dbPane.selectedQueryIds.includes(id)
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
                            font.pixelSize: Style.fontSmall
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            text: formattedDate || ""
                            font.family: Fonts.mono
                            font.pixelSize: Style.fontSmall
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

            StyledButton {
                text: qsTr("Delete")
                destructive: true
                icon.width: 24
                icon.height: 24
                icon.source: "qrc:/Resources/trash-can.svg"
                onClicked: {
                    if (dbPane.selectedQueryIds.length === 0) {
                        dbPane.showNoSelection();
                        return;
                    }
                    deleteQueriesDialog.open();
                }
            }
        }
    }

    // Delete queries confirmation dialog
    StyledDialog {
        id: deleteQueriesDialog
        anchors.centerIn: parent
        destructive: true
        width: 420
        title: qsTr("Delete Queries")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: deleteQueriesDialog.availableWidth
            text: qsTr("Delete the selected query(ies)? This cannot be undone.")
            wrapMode: Text.WordWrap
        }
        onAccepted: dbPane.deleteSelected()
        onRejected: footerLabel.text = qsTr("Deletion cancelled")
    }

    MessageBox {
        id: messageBox
    }
}
