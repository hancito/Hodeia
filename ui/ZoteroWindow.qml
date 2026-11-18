pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Zotero connector: Zotero's folder, the collections the project follows,
// and the new PDFs of those collections to import (or to ignore).
// HodeIA only reads Zotero.
StyledDialog {
    id: zoteroWindow
    width: 640
    height: 720
    anchors.centerIn: parent
    title: qsTr("Zotero")
    standardButtons: Dialog.Close

    // Proposal ids the user has unticked (all are ticked by default).
    property var unticked: ({})
    readonly property var selectedIds: {
        const ids = [];
        for (const p of zoteroController.proposals)
            if (!unticked[p.id])
                ids.push(p.id);
        return ids;
    }

    onOpened: {
        unticked = {};
        folderError.text = "";
        zoteroController.refresh();
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Zotero data folder")
        onAccepted: folderError.text = zoteroController.setDataDir(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ── Zotero's folder ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                color: zoteroController.dataDir ? Colors.text : Colors.red
                text: zoteroController.dataDir
                      ? qsTr("Zotero folder: %1").arg(zoteroController.dataDir)
                      : qsTr("Zotero was not found. Choose its data folder (the one holding zotero.sqlite).")
                wrapMode: zoteroController.dataDir ? Text.NoWrap : Text.WordWrap
            }
            StyledBusyIndicator {
                running: zoteroController.busy
                visible: zoteroController.busy
                Layout.preferredHeight: Style.controlHeight
                Layout.preferredWidth: Style.controlHeight
            }
            StyledButton {
                text: zoteroController.dataDir ? qsTr("Change...") : qsTr("Browse...")
                onClicked: folderDialog.open()
            }
        }
        StyledCheckBox {
            Layout.fillWidth: true
            text: qsTr("Name papers with Zotero's title, authors, year and journal (instead of the summary's)")
            checked: configManager.zoteroPriority
            onToggled: configManager.zoteroPriority = checked
        }
        Label {
            id: folderError
            Layout.fillWidth: true
            visible: text !== ""
            color: Colors.red
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: zoteroController.error !== ""
            text: zoteroController.error
            color: Colors.red
            wrapMode: Text.WordWrap
        }

        // ── Followed collections ──
        StyledGroupBox {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            title: qsTr("Collections followed by this project")
            enabled: zoteroController.dataDir !== "" && !zoteroController.busy

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                ListView {
                    id: collectionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: zoteroController.collections
                    ScrollBar.vertical: StyledScrollBar {}

                    delegate: ItemDelegate {
                        id: collectionRow
                        required property int index
                        required property string name
                        required property int depth
                        required property bool isLibrary
                        required property int checkState
                        required property int pdfCount
                        required property int onDiskCount
                        required property int inProjectCount

                        width: ListView.view.width
                        height: Style.rowHeight
                        leftPadding: 4 + depth * 18
                        background: Rectangle {
                            color: collectionRow.hovered ? Colors.surface0 : "transparent"
                            radius: Style.radiusSmall
                        }
                        onClicked: zoteroController.collections.toggle(index)

                        contentItem: RowLayout {
                            spacing: 8
                            CheckIndicator {
                                checked: collectionRow.checkState === Qt.Checked
                                partial: collectionRow.checkState === Qt.PartiallyChecked
                                hovered: collectionRow.hovered
                            }
                            Label {
                                Layout.fillWidth: true
                                text: collectionRow.name
                                font.bold: collectionRow.isLibrary
                                color: Colors.text
                                elide: Text.ElideRight
                            }
                            Label {
                                visible: !collectionRow.isLibrary
                                font.pixelSize: Style.fontSmall
                                color: Colors.overlay1
                                text: {
                                    let s = qsTr("%n PDF", "", collectionRow.pdfCount);
                                    if (collectionRow.inProjectCount > 0)
                                        s += " · " + qsTr("%n in the project", "", collectionRow.inProjectCount);
                                    const missing = collectionRow.pdfCount - collectionRow.onDiskCount;
                                    if (missing > 0)
                                        s += " · " + qsTr("%n not downloaded", "", missing);
                                    return s;
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        color: Colors.overlay1
                        font.pixelSize: Style.fontSmall
                        text: qsTr("Ticking a collection ticks its sub-collections.")
                        wrapMode: Text.WordWrap
                    }
                    StyledButton {
                        text: qsTr("Save Selection")
                        emphasized: true
                        onClicked: zoteroController.saveCollections()
                    }
                }
            }
        }

        // ── New PDFs ──
        StyledGroupBox {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 4
            title: qsTr("New PDFs (%1)").arg(zoteroController.proposals.length)
            enabled: !zoteroController.busy

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    visible: zoteroController.missingCount > 0
                    wrapMode: Text.WordWrap
                    color: Colors.overlay1
                    text: qsTr("%n PDF(s) of the followed collections are not on this computer. Download them in Zotero: they will be proposed here once downloaded.",
                               "", zoteroController.missingCount)
                }
                Label {
                    Layout.fillWidth: true
                    visible: zoteroController.proposals.length === 0
                    color: Colors.overlay1
                    wrapMode: Text.WordWrap
                    text: zoteroController.collections.followedCount === 0
                          ? qsTr("Follow collections above to have their PDFs proposed.")
                          : qsTr("Nothing new: every downloaded PDF of the followed collections is in the project or ignored.")
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: zoteroController.proposals
                    ScrollBar.vertical: StyledScrollBar {}

                    delegate: ItemDelegate {
                        id: proposalRow
                        required property var modelData
                        readonly property bool ticked: !zoteroWindow.unticked[modelData.id]

                        width: ListView.view.width
                        background: Rectangle {
                            color: proposalRow.hovered ? Colors.surface0 : "transparent"
                            radius: Style.radiusSmall
                        }
                        onClicked: {
                            const u = Object.assign({}, zoteroWindow.unticked);
                            u[modelData.id] = proposalRow.ticked;
                            zoteroWindow.unticked = u;
                        }

                        contentItem: RowLayout {
                            spacing: 8
                            CheckIndicator {
                                Layout.alignment: Qt.AlignTop
                                checked: proposalRow.ticked
                                hovered: proposalRow.hovered
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: proposalRow.modelData.title || qsTr("(untitled)")
                                    color: Colors.text
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    font.pixelSize: Style.fontSmall
                                    color: Colors.overlay1
                                    elide: Text.ElideRight
                                    text: {
                                        const m = proposalRow.modelData;
                                        let s = m.authors;
                                        if (m.year)
                                            s += (s ? " " : "") + "(" + m.year + ")";
                                        if (m.collections)
                                            s += (s ? " — " : "") + m.collections;
                                        return s;
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: zoteroController.proposals.length > 0
                    spacing: 8
                    StyledButton {
                        text: zoteroWindow.selectedIds.length === zoteroController.proposals.length
                              ? qsTr("Untick All") : qsTr("Tick All")
                        onClicked: {
                            if (zoteroWindow.selectedIds.length === zoteroController.proposals.length) {
                                const u = {};
                                for (const p of zoteroController.proposals)
                                    u[p.id] = true;
                                zoteroWindow.unticked = u;
                            } else {
                                zoteroWindow.unticked = {};
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    StyledButton {
                        text: qsTr("Ignore")
                        toolTip: qsTr("Never propose the ticked PDFs again")
                        enabled: zoteroWindow.selectedIds.length > 0
                        onClicked: zoteroController.ignoreProposals(zoteroWindow.selectedIds)
                    }
                    StyledButton {
                        text: qsTr("Import (%1)").arg(zoteroWindow.selectedIds.length)
                        emphasized: true
                        enabled: zoteroWindow.selectedIds.length > 0
                        onClicked: {
                            zoteroController.importProposals(zoteroWindow.selectedIds);
                            zoteroWindow.close();
                        }
                    }
                }
            }
        }
    }
}
