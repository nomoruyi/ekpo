import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Report a Bug, from any page (Main.qml). The text is exactly what GitHub gets, and
// the user can edit all of it; nothing leaves the machine until they submit the issue
// in the browser. Sandbox builds can also save it to the repo's bug-reports/.
Kirigami.Dialog {
    id: dialog

    property string preview: "" // the screenshot, as a data: URL; empty if the grab failed
    property string message: ""
    property int messageType: Kirigami.MessageType.Positive

    // where: the page and its state, as text lines.
    function start(screenshot, where) {
        preview = screenshot;
        message = "";
        titleField.text = "";
        withScreenshot.checked = screenshot !== "";
        extendedSystem.checked = false;
        withLog.checked = false;
        bodyArea.text = [qsTr("## What happened"), "", "", qsTr("## Steps to reproduce"), "1. ", "", qsTr("## What I expected"), "", "",
                         qsTr("## Where"), where, "", ""].join("\n") + generatedSections();
        open();
        titleField.forceActiveFocus();
    }
    // The System and Log sections at the end of the text, as the checkboxes ask. The
    // version, OS and kernel are always there; the rest only for bugs that need it.
    function generatedSections() {
        const parts = [qsTr("## System"), "```", Backend.systemReport(extendedSystem.checked), "```"];
        if (withLog.checked)
            parts.push("", qsTr("## Log"), "```", Backend.recentLog(), "```");
        return parts.join("\n");
    }
    // A checkbox changed: rewrite those sections and keep everything above them.
    function updateSections() {
        const text = bodyArea.text;
        const at = text.lastIndexOf("\n" + qsTr("## System") + "\n");
        bodyArea.text = (at >= 0 ? text.slice(0, at + 1) : text.replace(/\s*$/, "\n\n")) + generatedSections();
    }
    function showMessage(text, type) {
        messageType = type;
        message = text;
    }

    title: qsTr("Report a Bug")
    preferredWidth: Kirigami.Units.gridUnit * 42
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Cancel
    // (A list without Save, not a hidden action: Kirigami 6.24 can't lay out hidden actions.)
    customFooterActions: Backend.bugReportsDir !== "" ? [saveAction, githubAction] : [githubAction]

    Kirigami.Action {
        id: saveAction
        text: qsTr("Save Report")
        icon.name: "document-save"
        enabled: titleField.text.trim() !== ""
        onTriggered: {
            const result = Backend.saveBugReport(titleField.text.trim(), bodyArea.text, withScreenshot.checked);
            if (result.error)
                dialog.showMessage(result.error, Kirigami.MessageType.Error);
            else
                dialog.showMessage(qsTr("Saved as %1").arg(result.path), Kirigami.MessageType.Positive);
        }
    }
    Kirigami.Action {
        id: githubAction
        text: qsTr("Open on GitHub…")
        icon.name: "internet-web-browser"
        enabled: titleField.text.trim() !== ""
        onTriggered: {
            const error = Backend.openBugReport(titleField.text.trim(), bodyArea.text, withScreenshot.checked);
            if (error)
                dialog.showMessage(error, Kirigami.MessageType.Error);
            else
                dialog.close();
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.message !== ""
            type: dialog.messageType
            text: dialog.message
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Nothing is sent until you submit the issue on GitHub. This text is exactly what Ekpo fills in there: remove anything you'd rather not share, such as folder names.")
        }
        QQC2.TextField {
            id: titleField
            Layout.fillWidth: true
            placeholderText: qsTr("Short summary of the bug")
        }
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 12

            QQC2.TextArea {
                id: bodyArea
                wrapMode: Text.Wrap
                font.family: "monospace"
            }
        }
        RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Image {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                Layout.preferredHeight: Kirigami.Units.gridUnit * 4.5
                visible: dialog.preview !== ""
                opacity: withScreenshot.checked ? 1 : 0.3
                fillMode: Image.PreserveAspectFit
                source: dialog.preview
            }
            ColumnLayout {
                Layout.fillWidth: true

                QQC2.CheckBox {
                    id: withScreenshot
                    enabled: dialog.preview !== ""
                    text: dialog.preview !== "" ? qsTr("Include this screenshot of the window") : qsTr("No screenshot (the window couldn't be captured)")
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    visible: withScreenshot.checked
                    opacity: 0.7
                    text: qsTr("GitHub takes no images in a link, so Ekpo puts the screenshot on the clipboard: paste it into the issue text with Ctrl+V.")
                }
                QQC2.CheckBox {
                    id: extendedSystem
                    text: qsTr("Extended system info: boot setup, packages, kernels, library")
                    onToggled: dialog.updateSections()
                }
                QQC2.CheckBox {
                    id: withLog
                    text: qsTr("Logs: Ekpo's last messages and the helper's last lines")
                    onToggled: dialog.updateSections()
                }
            }
        }
    }
}
