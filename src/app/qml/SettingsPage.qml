import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// App preferences only (SPEC.md §4.13); boot options live on the Overview.
Kirigami.ScrollablePage {
    id: page

    signal review
    signal updateRequested

    property string removalMessage: ""
    property bool removalOk: false
    property bool removing: false
    property bool pruning: false
    property string pruneMessage: ""
    property string libraryMessage: ""
    property bool libraryOk: false

    FolderDialog {
        id: libraryDialog
        title: qsTr("Choose an empty folder, or one that holds an Ekpo library")
        onAccepted: {
            page.libraryMessage = Backend.setLibraryLocation(selectedFolder);
            page.libraryOk = false;
        }
    }
    // The whole library as one archive (SPEC.md §4.13).
    FileDialog {
        id: libraryExportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Archives (*.tar.gz)")]
        currentFile: StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/ekpo-library.tar.gz"
        onAccepted: {
            const error = Backend.exportLibraryTo(selectedFile);
            page.libraryOk = error === "";
            page.libraryMessage = error || qsTr("Saved. To use it elsewhere, unpack it and choose its folder with Change….");
        }
    }

    title: qsTr("Settings")
    actions: [
        ReportBugAction {}
    ]

    Connections {
        target: Backend
        function onApplyFinished(ok, error) {
            if (page.pruning) {
                page.pruning = false;
                page.pruneMessage = ok ? qsTr("Old theme copies removed.") : qsTr("Nothing was removed: %1").arg(error);
                return;
            }
            if (!page.removing)
                return;
            page.removing = false;
            page.removalOk = ok;
            page.removalMessage = ok
                ? qsTr("Ekpo's theme copies are removed and your boot look is back to Original. Remove the app itself with \u201csudo apt remove ekpo\u201d; your library stays in ~/.local/share/ekpo.")
                : qsTr("Nothing was removed: %1").arg(error);
        }
    }

    Kirigami.FormLayout {
        RowLayout {
            Kirigami.FormData.label: qsTr("Library:")
            QQC2.Label {
                text: Backend.libraryPath
                elide: Text.ElideMiddle
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            }
            QQC2.Button {
                text: qsTr("Change…")
                enabled: !Backend.applying
                onClicked: libraryDialog.open()
            }
            QQC2.Button {
                text: qsTr("Export…")
                icon.name: "document-export"
                onClicked: libraryExportDialog.open()
            }
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("An empty folder: the library moves there. A folder with an Ekpo library: Ekpo uses that one instead. Export saves the whole library as one archive.")
        }
        Kirigami.InlineMessage {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            Layout.fillWidth: true
            visible: page.libraryMessage !== ""
            type: page.libraryOk ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            text: page.libraryMessage
        }
        RowLayout {
            Kirigami.FormData.label: qsTr("History:")
            QQC2.SpinBox {
                from: 1
                to: 100
                value: Backend.historySize
                onValueModified: Backend.setHistorySize(value)
            }
            QQC2.Label {
                text: qsTr("entries kept, plus Original")
            }
        }
        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Boot splash changes:")
            text: qsTr("Rebuild all kernels by default")
            checked: Backend.rebuildAllKernels
            onToggled: Backend.setRebuildAllKernels(checked)
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("Off: applies rebuild only the newest kernel's initrd (faster, less Timeshift space), and the older kernels keep the previous splash as a fallback. Each review can change it. Restores always rebuild all kernels.")
        }
        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Reviews:")
            text: qsTr("Show file changes and commands")
            checked: Backend.technicalDetailsExpanded
            onToggled: Backend.setTechnicalDetailsExpanded(checked)
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: qsTr("Editor preview:")
            readonly property var resolutions: [1440, 1080, 720]
            model: resolutions.map(r => qsTr("%1p").arg(r))
            currentIndex: resolutions.indexOf(Backend.previewResolution)
            onActivated: index => Backend.setPreviewResolution(resolutions[index])
        }
        QQC2.Switch {
            Kirigami.FormData.label: qsTr("At start:")
            text: qsTr("Show the introduction")
            checked: Backend.showIntro
            onToggled: Backend.setShowIntro(checked)
        }
        // SPEC.md §4.14.
        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Updates:")
            text: qsTr("Check for a new version at start")
            enabled: Backend.updateInfo.canCheck === true
            checked: Backend.checkUpdates
            onToggled: Backend.setCheckUpdates(checked)
        }
        RowLayout {
            QQC2.Button {
                text: qsTr("Check Now")
                icon.name: "view-refresh"
                enabled: Backend.updateInfo.canCheck === true && Backend.updateInfo.checking !== true && Backend.updateInfo.busy !== true
                onClicked: Backend.checkForUpdates()
            }
            QQC2.Button {
                visible: Backend.updateInfo.available === true
                text: qsTr("Update…")
                icon.name: "update-none"
                onClicked: page.updateRequested()
            }
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.Wrap
            opacity: 0.7
            text: {
                const u = Backend.updateInfo;
                if (u.canCheck !== true)
                    return qsTr("This build doesn't check for updates.");
                if (u.checking === true)
                    return qsTr("Checking…");
                if (u.checkError)
                    return u.checkError;
                if (u.available === true)
                    return qsTr("Ekpo %1 is available; you have %2.").arg(u.version).arg(Qt.application.version);
                if (u.checked === true)
                    return qsTr("Ekpo %1 is the newest version.").arg(Qt.application.version);
                return qsTr("Asks GitHub for the newest release of Ekpo; nothing else is sent.");
            }
        }

        // SPEC.md §5: uninstalling the package leaves applied looks working; this
        // removes Ekpo's traces first.
        // Copies no history entry needs; applies remove them too (SPEC.md §4.9).
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Old theme copies")
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.Wrap
            text: Backend.oldCopies.length === 0
                ? qsTr("None: every installed copy is in use or needed by history.")
                : qsTr("%n copy(s) no history entry needs: %1", "", Backend.oldCopies.length).arg(Backend.oldCopies.join(", "))
        }
        QQC2.Button {
            text: qsTr("Remove Old Theme Copies")
            icon.name: "edit-clear-history"
            enabled: Backend.oldCopies.length > 0 && !Backend.applying
            QQC2.ToolTip.text: qsTr("Plasma splash copies go to the trash; boot menu and boot splash copies need your password.")
            QQC2.ToolTip.visible: hovered
            onClicked: {
                page.pruneMessage = "";
                page.pruning = Backend.removeOldCopies();
                if (!page.pruning)
                    page.pruneMessage = qsTr("Old theme copies moved to the trash.");
            }
        }
        QQC2.Label {
            visible: page.pruneMessage !== ""
            text: page.pruneMessage
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Remove Ekpo from this system")
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.Wrap
            text: qsTr("First restore the look from before Ekpo, then remove the theme copies Ekpo installed (%1 now) and its Plymouth registrations. Your library and imported themes stay.").arg(Backend.ekpoCopies)
        }
        RowLayout {
            QQC2.Button {
                text: qsTr("1. Restore Original…")
                enabled: !Backend.atOriginal && !Backend.applying
                onClicked: {
                    Backend.planRestoreOriginal();
                    page.review();
                }
            }
            QQC2.Button {
                text: qsTr("2. Remove Ekpo's Files…")
                enabled: Backend.atOriginal && Backend.ekpoCopies > 0 && !Backend.applying
                onClicked: confirmRemoval.open()
            }
        }
        Kirigami.InlineMessage {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            Layout.fillWidth: true
            visible: page.removalMessage !== ""
            type: page.removalOk ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            text: page.removalMessage
        }
    }

    Kirigami.PromptDialog {
        id: confirmRemoval

        title: qsTr("Remove Ekpo's Files?")
        subtitle: qsTr("Deletes Ekpo's %1 theme copies from /boot/grub/themes and /usr/share/plymouth/themes, removes their Plymouth registrations, and moves the Plasma splash copies Ekpo made to the trash. This asks for your password.").arg(Backend.ekpoCopies)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Remove")
                icon.name: "edit-delete"
                onTriggered: {
                    confirmRemoval.close();
                    page.removalMessage = "";
                    page.removing = true;
                    Backend.removeEkpoFiles();
                }
            }
        ]
    }
}
