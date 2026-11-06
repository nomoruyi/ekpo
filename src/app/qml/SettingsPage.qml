import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// App preferences only (SPEC.md §4.13); boot options live on the Overview.
Kirigami.ScrollablePage {
    id: page

    signal review

    property string removalMessage: ""
    property bool removalOk: false
    property bool removing: false

    title: qsTr("Settings")

    Connections {
        target: Backend
        function onApplyFinished(ok, error) {
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
        QQC2.Switch {
            Kirigami.FormData.label: qsTr("At start:")
            text: qsTr("Show the introduction")
            checked: Backend.showIntro
            onToggled: Backend.setShowIntro(checked)
        }

        // SPEC.md §5: uninstalling the package leaves applied looks working; this
        // removes Ekpo's traces first.
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
