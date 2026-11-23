// Blocking window shown while PDFs are imported: modal, cannot be dismissed
// (Escape / click outside), only cancelled.  Lists the import's operations —
// reading the files, extracting their text, indexing the papers — each
// with its own progress, under the overall progress.  Extraction and
// indexing run at the same time: papers are indexed as soon as extracted.
// Opens and closes itself from engineController's signals.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

StyledDialog {
    id: dialog

    // Overall: papers processed (added or failed) out of the new ones.
    property int papersDone: 0
    property int papersTotal: -1 // unknown until the files are read
    property int elapsedTenths: 0
    property bool cancelling: false

    // Files extracted so far (papersDone counts the indexed ones).
    property int extractDone: 0

    // Each paper counts half when its text is extracted and half once it is
    // indexed, so the bar moves with every file.
    readonly property real overallProgress: {
        if (papersTotal <= 0)
            return 0;
        return Math.min(1, (extractDone + papersDone) / (2 * papersTotal));
    }

    // One entry per operation, in pipeline order.  `phase` is "pending",
    // "running" or "done".
    ListModel {
        id: steps
    }

    function start() {
        papersDone = 0;
        papersTotal = -1;
        elapsedTenths = 0;
        cancelling = false;
        extractDone = 0;
        steps.clear();
        steps.append({
            key: "hash",
            label: qsTr("Reading the selected files"),
            unit: qsTr("files"),
            phase: "pending",
            current: 0,
            total: 0,
            detail: ""
        });
        steps.append({
            key: "extract",
            label: qsTr("Extracting text"),
            unit: qsTr("files"),
            phase: "pending",
            current: 0,
            total: 0,
            detail: ""
        });
        steps.append({
            key: "embed",
            label: qsTr("Indexing papers"),
            unit: qsTr("papers"),
            phase: "pending",
            current: 0,
            total: 0,
            detail: ""
        });
    }

    function indexOf(key) {
        for (let i = 0; i < steps.count; ++i) {
            if (steps.get(i).key === key)
                return i;
        }
        return -1;
    }

    function updateStep(key, current, total, detail) {
        const idx = indexOf(key);
        if (idx < 0)
            return;
        // Reading the files is over once the papers are extracted.
        if (key !== "hash") {
            const hash = indexOf("hash");
            if (steps.get(hash).phase === "running")
                steps.setProperty(hash, "phase", "done");
        }
        steps.setProperty(idx, "current", current);
        steps.setProperty(idx, "total", total);
        steps.setProperty(idx, "detail", detail);
        // Extraction and indexing run side by side: each is done on its own.
        steps.setProperty(idx, "phase", key !== "hash" && total > 0 && current >= total ? "done" : "running");
    }

    function finishSteps() {
        for (let i = 0; i < steps.count; ++i) {
            const s = steps.get(i);
            if (s.phase !== "pending")
                steps.setProperty(i, "phase", "done");
        }
    }

    function formatElapsed(tenths) {
        const totalSec = Math.floor(tenths / 10);
        if (totalSec >= 3600) {
            const h = Math.floor(totalSec / 3600);
            const m = Math.floor((totalSec % 3600) / 60);
            return qsTr("%1h %2m").arg(h).arg(m.toString().padStart(2, "0"));
        }
        if (totalSec >= 60) {
            const m = Math.floor(totalSec / 60);
            const s = totalSec % 60;
            return qsTr("%1m %2s").arg(m).arg(s.toString().padStart(2, "0"));
        }
        return qsTr("%1s").arg(totalSec);
    }

    anchors.centerIn: parent
    width: 520
    title: qsTr("Importing PDFs")
    closePolicy: Popup.NoAutoClose
    // Take keyboard focus away from the window behind.
    focus: true

    onOpened: elapsedTimer.start()
    onClosed: elapsedTimer.stop()

    Timer {
        id: elapsedTimer
        interval: 100
        repeat: true
        onTriggered: dialog.elapsedTenths += 1
    }

    Connections {
        target: engineController
        function onIngestionStarted() {
            dialog.start();
            dialog.open();
        }
        function onIngestionStepProgress(step, current, total, detail) {
            dialog.updateStep(step, current, total, detail);
            if (step === "extract")
                dialog.extractDone = current;
        }
        function onIngestionPapersProgress(done, total) {
            dialog.papersDone = done;
            dialog.papersTotal = total;
        }
        // The report window takes over as soon as the summary is ready;
        // ingestionFinished follows once the embedding model is released.
        function onIngestionReport(title, message, isError) {
            dialog.finishSteps();
            dialog.close();
        }
        function onIngestionFinished() {
            dialog.close();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        // ── Overall progress ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    font.bold: true
                    text: dialog.cancelling ? qsTr("Cancelling the import...") : dialog.papersTotal < 0 ? qsTr("Preparing the import...") : dialog.papersTotal === 0 ? qsTr("No new paper to import") : qsTr("Importing %n paper(s) — %1%", "", dialog.papersTotal).arg(Math.floor(dialog.overallProgress * 100))
                }
                Label {
                    font.family: Fonts.mono
                    font.pixelSize: Style.fontSmall
                    color: Colors.overlay2
                    text: dialog.formatElapsed(dialog.elapsedTenths)
                }
            }
            StyledProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: dialog.overallProgress
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Colors.surface0
        }

        // ── Operations ──
        Repeater {
            model: steps

            delegate: RowLayout {
                id: stepRow
                required property string label
                required property string unit
                required property string phase
                required property int current
                required property int total
                required property string detail

                Layout.fillWidth: true
                spacing: 10

                // State marker: spinner while running, check when done.
                Item {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    Layout.alignment: Qt.AlignTop

                    StyledBusyIndicator {
                        anchors.fill: parent
                        visible: running
                        running: stepRow.phase === "running"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        visible: stepRow.phase === "done"
                        width: 18
                        height: 18
                        radius: 9
                        color: Colors.green
                        Label {
                            anchors.centerIn: parent
                            text: "✓"
                            font.pixelSize: 12
                            font.bold: true
                            color: Colors.onColor(Colors.green)
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        visible: stepRow.phase === "pending"
                        width: 14
                        height: 14
                        radius: 7
                        color: "transparent"
                        border.width: 2
                        border.color: Colors.surface2
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: stepRow.label
                            color: stepRow.phase === "pending" ? Colors.overlay1 : Colors.text
                        }
                        Label {
                            visible: stepRow.total > 0
                            font.family: Fonts.mono
                            font.pixelSize: Style.fontSmall
                            color: Colors.overlay2
                            text: qsTr("%1/%2 %3").arg(stepRow.current).arg(stepRow.total).arg(stepRow.unit)
                        }
                    }
                    StyledProgressBar {
                        Layout.fillWidth: true
                        visible: stepRow.phase !== "pending"
                        from: 0
                        to: Math.max(1, stepRow.total)
                        value: stepRow.current
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: stepRow.detail !== "" && stepRow.phase !== "pending"
                        text: stepRow.detail
                        elide: Text.ElideMiddle
                        font.pixelSize: Style.fontSmall
                        color: Colors.overlay2
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: dialog.cancelling ? qsTr("Stopping after the current page or passage batch. Papers not fully indexed are not kept.") : qsTr("Please wait: scanned documents go through text recognition, which can take a while.")
                wrapMode: Text.WordWrap
                color: Colors.overlay1
                font.pixelSize: Style.fontSmall
            }
            StyledButton {
                text: dialog.cancelling ? qsTr("Cancelling...") : qsTr("Cancel")
                enabled: !dialog.cancelling
                onClicked: {
                    dialog.cancelling = true;
                    engineController.cancelEngine();
                }
            }
        }
    }
}
