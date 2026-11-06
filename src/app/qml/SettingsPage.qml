import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// App preferences only (SPEC.md §4.13); boot options live on the Overview.
Kirigami.ScrollablePage {
    id: page

    title: qsTr("Settings")

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
    }
}
