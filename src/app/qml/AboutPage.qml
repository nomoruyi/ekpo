import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Name, licence, logo credit, and a system report for bug reports (SPEC.md §4.1).
Kirigami.ScrollablePage {
    id: page

    signal intro

    property string report: ""

    title: qsTr("About Ekpo")

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Image {
                source: "qrc:/io.github.nomoruyi.ekpo.svg"
                sourceSize.width: Kirigami.Units.iconSizes.enormous
                sourceSize.height: Kirigami.Units.iconSizes.enormous
            }
            ColumnLayout {
                Kirigami.Heading {
                    level: 1
                    text: "Ekpo " + Qt.application.version
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Design and switch the look of the boot menu, the boot splash and the Plasma splash.")
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.7
                    text: qsTr("\u201cEkpo\u201d is Edo (Bini) for mask. The logo is drawn after the 16th-century ivory pendant mask of Queen Mother Idia of the Benin Kingdom.")
                }
                QQC2.Label {
                    opacity: 0.7
                    text: qsTr("MIT License \u00a9 2026 nomoruyi")
                }
            }
        }

        RowLayout {
            QQC2.Button {
                icon.name: "edit-copy"
                text: qsTr("Copy System Report")
                onClicked: {
                    page.report = Backend.systemReport();
                    Backend.copyToClipboard(page.report);
                }
            }
            QQC2.Button {
                icon.name: "help-about"
                text: qsTr("Show Introduction")
                onClicked: page.intro()
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: page.report !== ""
            text: qsTr("Copied to the clipboard:")
        }
        QQC2.TextArea {
            Layout.fillWidth: true
            visible: page.report !== ""
            readOnly: true
            wrapMode: Text.WrapAnywhere
            font.family: "monospace"
            text: page.report
        }
    }
}
