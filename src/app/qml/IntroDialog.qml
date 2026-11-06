import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Shown on start until dismissed for good, and from the Overview (SPEC.md §4.2).
Kirigami.Dialog {
    id: dialog

    title: qsTr("Welcome to Ekpo")
    preferredWidth: Kirigami.Units.gridUnit * 34
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Close
    onOpened: dontShow.checked = !Backend.showIntro
    onClosed: Backend.setShowIntro(!dontShow.checked)

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        component Section: ColumnLayout {
            id: section

            required property string heading
            required property string body

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading {
                level: 3
                text: section.heading
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                text: section.body
            }
        }

        Section {
            heading: qsTr("Three stages, one look")
            body: qsTr("Ekpo changes what you see while the computer starts: the <b>boot menu</b> (GRUB), the <b>boot splash</b> (Plymouth) and the <b>Plasma splash</b> after you log in. Import themes, group them into Masks, and apply them together.")
        }
        Section {
            heading: qsTr("Before your first apply")
            body: qsTr("Every apply is checked, shown to you first and can be undone, but the boot menu and splash are part of starting the computer. So once, before the first try:<br>"
                + "• take a Timeshift snapshot (Timeshift → Create), and<br>"
                + "• have a bootable live USB at hand.")
        }
        Section {
            heading: qsTr("Getting back to a working look")
            body: qsTr("• <b>In Ekpo:</b> History → Restore, or Restore Original for the look from before Ekpo.<br>"
                + "• <b>From a terminal:</b> <tt>ekpo restore original</tt>, or the steps in the entry's RESTORE.txt.<br>"
                + "• <b>If the splash misbehaves:</b> pick an older kernel under “Advanced options” in the boot menu.<br>"
                + "• <b>If nothing helps:</b> boot the live USB and restore the Timeshift snapshot.")
        }
        QQC2.CheckBox {
            id: dontShow
            text: qsTr("Don't show this again")
        }
    }
}
