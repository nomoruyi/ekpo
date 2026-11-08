pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Masks: one theme (or "leave as is") per stage, applied together (SPEC.md §4.8).
Kirigami.ScrollablePage {
    id: page

    signal review

    readonly property var stages: [
        { key: "bootMenu", title: qsTr("Boot menu") },
        { key: "bootSplash", title: qsTr("Boot splash") },
        { key: "plasmaSplash", title: qsTr("Plasma splash") }
    ]
    property string message: ""
    // Shown imperatively: the close button sets visible = false, which would remove a binding.
    onMessageChanged: messageBox.visible = message !== ""

    title: qsTr("Masks")
    actions: [
        Kirigami.Action {
            text: qsTr("New Mask…")
            icon.name: "list-add"
            onTriggered: maskDialog.edit(null)
        },
        ReportBugAction {}
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            id: messageBox
            Layout.fillWidth: true
            visible: false
            type: Kirigami.MessageType.Warning
            showCloseButton: true
            text: page.message
            onVisibleChanged: if (!visible) page.message = ""
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: Backend.masks.length === 0
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("No Masks yet. A Mask picks one design or imported theme per stage, or leaves a stage as it is, and applies them in one go.")
        }
        Repeater {
            model: Backend.masks
            delegate: Kirigami.AbstractCard {
                id: maskCard

                required property var modelData

                Layout.fillWidth: true
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 3
                            elide: Text.ElideRight
                            text: maskCard.modelData.name
                        }
                        StatusLine {
                            Layout.fillWidth: true
                            visible: maskCard.modelData.update
                            level: "info"
                            text: qsTr("Update available: a design in it has changed since it was applied.")
                        }
                        Repeater {
                            model: page.stages
                            delegate: QQC2.Label {
                                required property var modelData
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: "%1: %2".arg(modelData.title).arg(maskCard.modelData.slots[modelData.key].name ?? qsTr("leave as is"))
                            }
                        }
                    }
                    QQC2.Button {
                        icon.name: maskCard.modelData.update ? "update-none" : "dialog-ok-apply"
                        text: maskCard.modelData.update ? qsTr("Update…") : qsTr("Apply…")
                        enabled: !Backend.applying
                        onClicked: {
                            Backend.planApplyMask(maskCard.modelData.id);
                            page.review();
                        }
                    }
                    QQC2.ToolButton {
                        icon.name: "document-edit"
                        text: qsTr("Edit")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: maskDialog.edit(maskCard.modelData)
                    }
                    QQC2.ToolButton {
                        icon.name: "document-export"
                        text: qsTr("Export…")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: exportDialog.start("mask", maskCard.modelData.id, maskCard.modelData.name)
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        text: qsTr("Move to Trash")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: page.message = Backend.removeMask(maskCard.modelData.id)
                    }
                }
            }
        }
    }

    component StagePicker: QQC2.ComboBox {
        required property var modelData
        readonly property string stageKey: modelData.key
        // Designs first, then imports; broken imports can't be applied, so they aren't offered.
        readonly property var choices: [{ id: "", name: qsTr("Leave as is") }].concat(
            Backend.designs.filter(d => d.stage === modelData.key && d.version > 0).map(d => ({ id: d.id, name: d.name })),
            Backend.imports.filter(t => t.stage === modelData.key && t.verdict !== "broken").map(t => ({ id: t.id, name: t.name })))

        Kirigami.FormData.label: modelData.title + ":"
        model: choices.map(c => c.name)
    }

    Kirigami.Dialog {
        id: maskDialog

        property string maskId: ""

        function edit(mask) {
            maskId = mask ? mask.id : "";
            nameField.text = mask ? mask.name : "";
            for (let i = 0; i < pickers.count; ++i) {
                const picker = pickers.itemAt(i) as StagePicker;
                const ref = mask ? (mask.slots[picker.stageKey].ref ?? "") : "";
                picker.currentIndex = Math.max(0, picker.choices.findIndex(c => c.id === ref));
            }
            open();
        }

        title: maskId ? qsTr("Edit Mask") : qsTr("New Mask")
        preferredWidth: Kirigami.Units.gridUnit * 26
        padding: Kirigami.Units.largeSpacing
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Save")
                icon.name: "document-save"
                enabled: nameField.text.trim() !== ""
                onTriggered: {
                    const refs = [];
                    for (let i = 0; i < pickers.count; ++i) {
                        const picker = pickers.itemAt(i) as StagePicker;
                        refs.push(picker.choices[picker.currentIndex].id);
                    }
                    page.message = Backend.saveMask(maskDialog.maskId, nameField.text, refs[0], refs[1], refs[2]);
                    maskDialog.close();
                }
            }
        ]

        Kirigami.FormLayout {
            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: qsTr("Name:")
            }
            Repeater {
                id: pickers

                model: page.stages
                delegate: StagePicker {}
            }
        }
    }

    ExportDialog {
        id: exportDialog
    }
}
