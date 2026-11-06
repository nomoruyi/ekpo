pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Start page: the current look of each stage, in boot order (SPEC.md §4.1).
Kirigami.ScrollablePage {
    id: page

    signal openStage(string stage)
    signal review
    signal intro

    readonly property var stageTitles: ({ bootMenu: qsTr("Boot Menu"), bootSplash: qsTr("Boot Splash"), plasmaSplash: qsTr("Plasma Splash") })

    title: qsTr("Overview")
    actions: [
        Kirigami.Action {
            text: qsTr("Introduction")
            icon.name: "help-about"
            onTriggered: page.intro()
        },
        Kirigami.Action {
            text: qsTr("Refresh")
            icon.name: "view-refresh"
            onTriggered: Backend.refresh()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: Backend.welcomeBack
            type: Kirigami.MessageType.Information
            text: qsTr("Welcome back. Your Ekpo library is here, but this system doesn't have the look Ekpo last set; it looks like a fresh install. Re-apply your last look (themes that were installed by hand may need importing again), or start fresh from what is on this system now.")
            actions: [
                Kirigami.Action {
                    text: qsTr("Re-apply My Last Look…")
                    icon.name: "edit-redo"
                    enabled: !Backend.applying
                    onTriggered: {
                        Backend.planReapplyLastLook();
                        page.review();
                    }
                },
                Kirigami.Action {
                    text: qsTr("Start Fresh")
                    icon.name: "document-new"
                    onTriggered: Backend.startFresh()
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: Backend.system.leftoverJournal
            type: Kirigami.MessageType.Error
            text: qsTr("An earlier apply was cut off. Roll it back to return to the state from before it.")
            actions: [
                Kirigami.Action {
                    text: qsTr("Roll Back Now")
                    icon.name: "edit-undo"
                    enabled: !Backend.applying
                    onTriggered: Backend.recover()
                }
            ]
        }
        Repeater {
            model: Backend.system.errors
            delegate: Kirigami.InlineMessage {
                required property string modelData
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Warning
                text: modelData
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > Kirigami.Units.gridUnit * 42 ? 3 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: Backend.stageCards
                delegate: Kirigami.AbstractCard {
                    id: stageCard

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1 // equal columns

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: page.stageTitles[stageCard.modelData.stage]
                            font.capitalization: Font.AllUppercase
                            opacity: 0.7
                        }
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 2
                            text: stageCard.modelData.current
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: stageCard.modelData.lines
                            delegate: StatusLine {
                                required property var modelData
                                Layout.fillWidth: true
                                level: modelData.level
                                text: modelData.text
                            }
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            layoutDirection: Qt.RightToLeft
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Button {
                                text: qsTr("Change…")
                                onClicked: page.openStage(stageCard.modelData.stage)
                            }
                            QQC2.Button {
                                visible: stageCard.modelData.stage === "bootMenu"
                                text: qsTr("Timeout…")
                                enabled: !Backend.applying
                                onClicked: {
                                    timeoutDialog.seconds = stageCard.modelData.timeout;
                                    timeoutDialog.open();
                                }
                            }
                            QQC2.Button {
                                visible: stageCard.modelData.drift
                                text: qsTr("Re-apply…")
                                enabled: !Backend.applying
                                onClicked: {
                                    Backend.planReapplyStage(stageCard.modelData.stage);
                                    page.review();
                                }
                            }
                            QQC2.Button {
                                visible: stageCard.modelData.drift
                                text: qsTr("Keep This")
                                QQC2.ToolTip.text: qsTr("Make the current state Ekpo's new reference")
                                QQC2.ToolTip.visible: hovered
                                onClicked: Backend.adoptStage(stageCard.modelData.stage)
                            }
                        }
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            opacity: 0.7
            wrapMode: Text.Wrap
            text: qsTr("Initrds are built by %1.").arg(Backend.system.generator || qsTr("an unknown tool"))
        }
    }

    // The GRUB menu timeout, one machine-wide value (SPEC.md §4.10).
    Kirigami.Dialog {
        id: timeoutDialog

        property int seconds: 20

        title: qsTr("Boot Menu Timeout")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Review…")
                icon.name: "dialog-ok-apply"
                enabled: spin.value > 0 || liveUsb.checked
                onTriggered: {
                    timeoutDialog.close();
                    Backend.planTimeout(spin.value);
                    page.review();
                }
            }
        ]
        onOpened: {
            spin.value = seconds;
            liveUsb.checked = false;
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                QQC2.Label {
                    text: qsTr("Show the boot menu for")
                }
                QQC2.SpinBox {
                    id: spin
                    from: 0
                    to: 600
                }
                QQC2.Label {
                    text: qsTr("seconds")
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: spin.value === 0
                text: qsTr("With 0 s the computer boots straight through. On this system GRUB can't detect failed boots, so there is no way back into the menu, the older kernels or recovery mode without a live USB.")
            }
            QQC2.CheckBox {
                id: liveUsb
                visible: spin.value === 0
                text: qsTr("I have a live USB and can recover with it")
            }
        }
    }
}
