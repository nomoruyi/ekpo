pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Export a design or a Mask (SPEC.md §4.13): a plain package anyone can install without
// Ekpo, in a .tar.gz. Lists what isn't the owner's own work, so they can check the
// licences before sharing; nothing is blocked.
Kirigami.Dialog {
    id: dialog

    property string kind // design, mask
    property string itemId
    property string name
    property var plan: ({})
    property string message: ""
    property bool saved: false

    function start(exportKind, id, title) {
        kind = exportKind;
        itemId = id;
        name = title;
        message = "";
        saved = false;
        plan = Backend.planExport(kind, itemId, "");
        open();
    }

    title: kind === "mask" ? qsTr("Export the Mask “%1”").arg(name) : qsTr("Export “%1”").arg(name)
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Close
    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Save As…")
            icon.name: "document-save-as"
            enabled: (dialog.plan.problems ?? []).length === 0
            onTriggered: saveDialog.open()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.kind === "mask"
                ? qsTr("A folder per stage, each a plain theme package with a README that says how to install it, also where Ekpo isn't installed.")
                : qsTr("A plain theme package with a README that says how to install it, also where Ekpo isn't installed.")
        }
        RowLayout {
            visible: dialog.plan.needsScreen === true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: qsTr("Built for the screen size")
            }
            QQC2.ComboBox {
                model: dialog.plan.screens ?? []
                currentIndex: (dialog.plan.screens ?? []).indexOf(dialog.plan.screen ?? "")
                onActivated: index => dialog.plan = Backend.planExport(dialog.kind, dialog.itemId, model[index])
            }
        }
        Repeater {
            model: dialog.plan.problems ?? []
            delegate: Kirigami.InlineMessage {
                required property string modelData
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Error
                text: modelData
            }
        }

        // The licence check (ARCHITECTURE.md §9d).
        Kirigami.Heading {
            level: 4
            visible: (dialog.plan.thirdParty ?? []).length > 0
            text: qsTr("Not your own work")
        }
        Repeater {
            model: dialog.plan.thirdParty ?? []
            delegate: RowLayout {
                id: item

                required property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    source: item.modelData.free ? "emblem-ok-symbolic" : "data-warning"
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: item.modelData.free
                        ? qsTr("The %1 “%2”: %3").arg(item.modelData.kind).arg(item.modelData.name).arg(item.modelData.licence)
                        : qsTr("The %1 “%2”: %3, check before sharing").arg(item.modelData.kind).arg(item.modelData.name)
                              .arg(item.modelData.licence || qsTr("licence unknown"))
                    QQC2.ToolTip.text: item.modelData.source
                    QQC2.ToolTip.visible: item.modelData.source !== "" && hover.hovered
                    HoverHandler {
                        id: hover
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.message !== ""
            type: dialog.saved ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            text: dialog.message
        }
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Archives (*.tar.gz)")]
        currentFile: StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + (dialog.plan.folder ?? "export") + ".tar.gz"
        onAccepted: {
            const error = Backend.writeExport(selectedFile);
            dialog.saved = error === "";
            dialog.message = error || qsTr("Saved.");
        }
    }
}
