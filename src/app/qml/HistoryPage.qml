pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// Every apply and restore, newest first, plus Original (SPEC.md §4.11).
Kirigami.ScrollablePage {
    id: page

    signal review

    readonly property var stageTitles: ({ bootMenu: qsTr("Boot menu"), bootSplash: qsTr("Boot splash"), plasmaSplash: qsTr("Plasma splash") })

    title: qsTr("History")
    actions: [
        ReportBugAction {}
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading {
                        level: 3
                        text: qsTr("Original")
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.8
                        text: Backend.originalAvailable
                            ? qsTr("The look each stage had before Ekpo first changed it.")
                            : qsTr("Saved automatically right before Ekpo first changes a stage.")
                    }
                }
                QQC2.Button {
                    enabled: Backend.originalAvailable && !Backend.applying
                    text: qsTr("Restore Original…")
                    onClicked: {
                        Backend.planRestoreOriginal();
                        page.review();
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: Backend.history.length === 0
            opacity: 0.7
            wrapMode: Text.Wrap
            text: qsTr("No applies yet. Each apply becomes an entry here that you can restore.")
        }

        Repeater {
            model: Backend.history
            delegate: Kirigami.AbstractCard {
                id: entryCard

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
                            text: "#%1 %2".arg(entryCard.modelData.number).arg(entryCard.modelData.title)
                        }
                        QQC2.Label {
                            opacity: 0.7
                            text: entryCard.modelData.createdAt.toLocaleString(Qt.locale(), Locale.ShortFormat)
                        }
                        Repeater {
                            model: entryCard.modelData.stages
                            delegate: QQC2.Label {
                                required property var modelData
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: "%1: %2".arg(page.stageTitles[modelData.stage]).arg(modelData.label)
                            }
                        }
                    }
                    QQC2.ToolButton {
                        icon.name: "document-preview"
                        text: qsTr("Console steps")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: qsTr("Open RESTORE.txt: how to go back without Ekpo")
                        QQC2.ToolTip.visible: hovered
                        onClicked: Qt.openUrlExternally("file://" + entryCard.modelData.restoreFile)
                    }
                    QQC2.Button {
                        enabled: !Backend.applying
                        text: qsTr("Restore…")
                        onClicked: {
                            Backend.planRestoreEntry(entryCard.modelData.number);
                            page.review();
                        }
                    }
                }
            }
        }
    }
}
