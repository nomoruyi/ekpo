import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Update to a newer release (SPEC.md §4.14): its notes, then download, checks,
// install with the password prompt, and restart. Opened from the Overview or Settings.
Kirigami.Dialog {
    id: dialog

    readonly property var info: Backend.updateInfo

    title: qsTr("Update to Ekpo %1").arg(info.version ?? "")
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Close
    // (Lists, not hidden actions: Kirigami 6.24 can't lay out hidden actions.)
    customFooterActions: info.installed === true ? [restartAction] : [updateAction]
    // Closing stops a download; an install that has started runs to its end.
    onRejected: Backend.cancelUpdate()

    Kirigami.Action {
        id: updateAction
        text: qsTr("Update (asks for your password)")
        icon.name: "update-none"
        enabled: dialog.info.busy !== true && !Backend.applying
        onTriggered: Backend.startUpdate()
    }
    Kirigami.Action {
        id: restartAction
        text: qsTr("Restart Ekpo")
        icon.name: "view-refresh"
        onTriggered: Backend.restartApp()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("You have Ekpo %1. What's new:").arg(Qt.application.version)
        }
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8

            QQC2.TextArea {
                readOnly: true
                wrapMode: Text.Wrap
                textFormat: TextEdit.MarkdownText
                text: dialog.info.notes || qsTr("(No notes for this release.)")
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: dialog.info.busy === true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: dialog.info.text ?? ""
            }
            QQC2.ProgressBar {
                Layout.fillWidth: true
                indeterminate: (dialog.info.fraction ?? -1) < 0
                value: Math.max(0, dialog.info.fraction ?? 0)
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: Backend.applying
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("An apply is running. Update once it has finished.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: (dialog.info.error ?? "") !== ""
            type: Kirigami.MessageType.Error
            text: dialog.info.error ?? ""
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.info.installed === true
            type: Kirigami.MessageType.Positive
            text: qsTr("Ekpo %1 is installed. Restart Ekpo to use it.").arg(dialog.info.version ?? "")
        }
    }
}
