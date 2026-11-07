pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Shows Backend.plan before anything changes, then the apply's progress and
// result (SPEC.md §4.9). Replaces the current page until it is closed.
Kirigami.ScrollablePage {
    id: page

    signal done

    readonly property var plan: Backend.plan
    readonly property var stageTitles: ({ bootMenu: qsTr("Boot menu"), bootSplash: qsTr("Boot splash"), plasmaSplash: qsTr("Plasma splash") })
    property var progressLines: []
    property var outcome: null // {ok, error, number} once finished

    function reset() {
        progressLines = [];
        outcome = null;
        details.checked = Backend.technicalDetailsExpanded;
    }

    title: plan.title ? qsTr("Apply “%1”").arg(plan.title) : ""
    actions: [
        ReportBugAction {}
    ]

    Connections {
        target: Backend
        function onApplyProgress(text) {
            page.progressLines = page.progressLines.concat([text]);
        }
        function onApplyFinished(ok, error, number) {
            page.outcome = { ok: ok, error: error, number: number };
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: page.plan.problems ?? []
            delegate: Kirigami.InlineMessage {
                required property string modelData
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Error
                text: modelData
            }
        }

        Repeater {
            model: page.plan.stages ?? []
            delegate: Kirigami.AbstractCard {
                id: stageCard

                required property var modelData
                required property int index

                Layout.fillWidth: true
                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        wrapMode: Text.Wrap
                        text: qsTr("%1. %2: %3 → %4").arg(stageCard.index + 1).arg(page.stageTitles[stageCard.modelData.stage])
                            .arg(stageCard.modelData.from).arg(stageCard.modelData.to)
                    }
                    Repeater {
                        model: stageCard.modelData.actions
                        delegate: QQC2.Label {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "• " + modelData
                        }
                    }
                    Repeater {
                        model: details.checked ? stageCard.modelData.diffs : []
                        delegate: ColumnLayout {
                            id: diffBlock

                            required property var modelData

                            Layout.fillWidth: true
                            QQC2.Label {
                                font.bold: true
                                text: diffBlock.modelData.path
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                font.family: "monospace"
                                wrapMode: Text.WrapAnywhere
                                textFormat: Text.PlainText
                                text: (diffBlock.modelData.before ? diffBlock.modelData.before.trim().split("\n").map(l => "- " + l).join("\n") + "\n" : qsTr("(new file)") + "\n")
                                    + (diffBlock.modelData.after ? diffBlock.modelData.after.trim().split("\n").map(l => "+ " + l).join("\n") : qsTr("(deleted)"))
                            }
                        }
                    }
                }
            }
        }

        Repeater {
            model: page.plan.notes ?? []
            delegate: StatusLine {
                required property string modelData
                Layout.fillWidth: true
                level: "info"
                text: modelData
            }
        }
        StatusLine {
            Layout.fillWidth: true
            visible: (page.plan.estimatedSeconds ?? 0) > 0
            level: "info"
            text: qsTr("Takes about %1 s. Shutdown and sleep are blocked meanwhile; closing Ekpo doesn't stop it.").arg(page.plan.estimatedSeconds)
        }

        // Only for boot splash changes: rebuild every kernel's initrd or the newest one's.
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.plan.touchesSplash === true && (page.plan.kernelCount ?? 0) > 1 && page.outcome === null
            spacing: 0

            QQC2.Switch {
                text: qsTr("Rebuild the initrds of all %1 kernels").arg(page.plan.kernelCount)
                checked: page.plan.allKernels === true
                enabled: !Backend.applying
                onToggled: Backend.setPlanAllKernels(checked)
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit * 2
                wrapMode: Text.Wrap
                opacity: 0.7
                text: page.plan.allKernels
                    ? qsTr("Every kernel gets the new splash.")
                    : qsTr("Off: only %1 gets the new splash. The older kernels keep the current one, so you can boot one of them from \u201cAdvanced options\u201d if the new splash misbehaves.").arg(page.plan.newestKernel)
            }
        }

        // Designs are built for one screen size (SPEC.md §4.5): the primary screen's, or another.
        RowLayout {
            visible: page.plan.screen !== undefined && page.outcome === null
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: qsTr("Built for the screen size")
            }
            QQC2.ComboBox {
                id: screenBox
                model: page.plan.screens ?? []
                currentIndex: (page.plan.screens ?? []).indexOf(page.plan.screen ?? "")
                enabled: !Backend.applying
                onActivated: index => Backend.setPlanScreen(model[index])
            }
            QQC2.Label {
                visible: screenBox.currentIndex === 0
                opacity: 0.7
                text: qsTr("(your primary screen)")
            }
        }

        QQC2.CheckBox {
            id: details
            text: qsTr("Show file changes and commands")
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: details.checked && (page.plan.commands ?? []).length > 0
            font.family: "monospace"
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            text: (page.plan.commands ?? []).map(c => "# " + c).join("\n")
        }

        // Progress and result.
        ColumnLayout {
            Layout.fillWidth: true
            visible: Backend.applying || page.outcome !== null
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                visible: Backend.applying
                QQC2.BusyIndicator {
                    running: Backend.applying
                }
                QQC2.Label {
                    text: page.progressLines.length > 0 ? page.progressLines[page.progressLines.length - 1] : qsTr("Starting…")
                }
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: page.outcome !== null
                type: page.outcome && page.outcome.ok ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
                text: !page.outcome ? "" : page.outcome.ok
                    ? (page.outcome.error || qsTr("Done. Saved as history entry #%1; you can restore the previous look from History.").arg(page.outcome.number))
                    : qsTr("Nothing was changed: %1").arg(page.outcome.error)
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: page.outcome !== null && page.progressLines.length > 0
                opacity: 0.7
                wrapMode: Text.Wrap
                text: page.progressLines.join("\n")
            }
        }
    }

    footer: QQC2.ToolBar {
        contentItem: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            QQC2.Button {
                text: page.outcome ? qsTr("Close") : qsTr("Cancel")
                enabled: !Backend.applying
                onClicked: page.done()
            }
            QQC2.Button {
                visible: page.outcome === null
                enabled: page.plan.canApply === true && !Backend.applying
                icon.name: page.plan.needsPassword ? "dialog-password" : "dialog-ok-apply"
                text: page.plan.needsPassword ? qsTr("Apply (asks for your password)") : qsTr("Apply")
                onClicked: Backend.applyPlan()
            }
        }
    }
}
