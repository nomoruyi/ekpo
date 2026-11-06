pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// One stage's themes: imported ones, and installed ones not imported yet (SPEC.md §4.1, §4.3).
Kirigami.ScrollablePage {
    id: page

    required property string stage

    signal review

    readonly property var imports: Backend.imports.filter(t => t.stage === stage)
    readonly property var installed: Backend.installed.filter(t => t.stage === stage)
    readonly property var verdicts: ({
        works: { level: "ok", text: qsTr("Works") },
        warnings: { level: "warning", text: qsTr("Works, with warnings") },
        repaired: { level: "info", text: qsTr("Repaired on import") },
        broken: { level: "error", text: qsTr("Broken: can't be applied") }
    })
    property var lastResult: null

    function importUrl(url) {
        lastResult = Backend.importPath(url);
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Import Folder…")
            icon.name: "document-open-folder"
            onTriggered: folderDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Import Archive…")
            icon.name: "package-x-generic"
            onTriggered: archiveDialog.open()
        }
    ]

    FolderDialog {
        id: folderDialog
        onAccepted: page.importUrl(selectedFolder)
    }
    FileDialog {
        id: archiveDialog
        nameFilters: [qsTr("Theme archives (*.tar *.tar.gz *.tgz *.tar.xz *.txz *.tar.zst *.tar.bz2 *.zip)")]
        onAccepted: page.importUrl(selectedFile)
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.lastResult !== null
            showCloseButton: true
            onVisibleChanged: if (!visible) page.lastResult = null
            type: page.lastResult && page.lastResult.errors.length > 0 ? Kirigami.MessageType.Warning : Kirigami.MessageType.Positive
            text: {
                if (!page.lastResult)
                    return "";
                const parts = [];
                if (page.lastResult.names.length > 0)
                    parts.push(qsTr("Imported: %1").arg(page.lastResult.names.join(", ")));
                return parts.concat(page.lastResult.errors).join("\n");
            }
        }

        Kirigami.Heading {
            level: 2
            text: qsTr("In your library")
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: page.imports.length === 0
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("Nothing imported yet. Import a theme folder or archive, or one of the themes installed on this system.")
        }
        Repeater {
            model: page.imports
            delegate: Kirigami.AbstractCard {
                id: themeCard

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
                            text: themeCard.modelData.name
                            elide: Text.ElideRight
                        }
                        StatusLine {
                            Layout.fillWidth: true
                            level: page.verdicts[themeCard.modelData.verdict].level
                            text: page.verdicts[themeCard.modelData.verdict].text
                                + " · " + (themeCard.modelData.installedId ? qsTr("uses the installed copy") : qsTr("installs as a copy"))
                        }
                    }
                    QQC2.Button {
                        enabled: themeCard.modelData.verdict !== "broken" && !Backend.applying
                        icon.name: "dialog-ok-apply"
                        text: qsTr("Apply…")
                        onClicked: {
                            Backend.planApplyImport(themeCard.modelData.id);
                            page.review();
                        }
                    }
                    QQC2.Button {
                        text: qsTr("Report")
                        icon.name: "documentinfo"
                        onClicked: {
                            reportDialog.theme = themeCard.modelData;
                            reportDialog.open();
                        }
                    }
                    QQC2.ToolButton {
                        icon.name: "view-refresh"
                        text: qsTr("Re-import")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: page.lastResult = Backend.reimport(themeCard.modelData.id)
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        text: qsTr("Move to Trash")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: {
                            const error = Backend.removeImport(themeCard.modelData.id);
                            page.lastResult = { names: [], errors: error ? [error] : [] };
                        }
                    }
                }
            }
        }

        Kirigami.Heading {
            level: 2
            visible: page.installed.length > 0
            text: qsTr("Installed on this system, not imported")
        }
        Repeater {
            model: page.installed
            delegate: RowLayout {
                id: installedRow

                required property var modelData

                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        text: installedRow.modelData.name
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: installedRow.modelData.path
                        elide: Text.ElideMiddle
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                    }
                }
                QQC2.Button {
                    text: qsTr("Import")
                    onClicked: page.importUrl("file://" + installedRow.modelData.path)
                }
            }
        }
    }

    Kirigami.Dialog {
        id: reportDialog

        property var theme: null

        title: theme ? qsTr("Import report: %1").arg(theme.name) : ""
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 30
        padding: Kirigami.Units.largeSpacing

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            StatusLine {
                Layout.fillWidth: true
                level: reportDialog.theme ? page.verdicts[reportDialog.theme.verdict].level : "info"
                text: reportDialog.theme ? page.verdicts[reportDialog.theme.verdict].text : ""
            }
            Repeater {
                model: reportDialog.theme ? reportDialog.theme.report : []
                delegate: QQC2.Label {
                    required property string modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "• " + modelData
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: reportDialog.theme ? qsTr("Source: %1").arg(reportDialog.theme.sourcePath) : ""
            }
        }
    }
}
