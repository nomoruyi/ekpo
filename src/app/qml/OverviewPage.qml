pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Personae

// Start page: the current look of each stage, in boot order (SPEC.md §4.1).
Kirigami.ScrollablePage {
    id: page

    signal openStage(string stage)

    readonly property var stageTitles: ({ bootMenu: qsTr("Boot Menu"), bootSplash: qsTr("Boot Splash"), plasmaSplash: qsTr("Plasma Splash") })

    title: qsTr("Overview")
    actions: [
        Kirigami.Action {
            text: qsTr("Refresh")
            icon.name: "view-refresh"
            onTriggered: Backend.refresh()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: Backend.system.leftoverJournal
            type: Kirigami.MessageType.Error
            text: qsTr("An earlier apply was cut off. Personae will offer to roll it back.")
        }
        Repeater {
            model: Backend.system.errors
            delegate: Kirigami.InlineMessage {
                required property string modelData
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Warning
                text: modelData
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > Kirigami.Units.gridUnit * 42 ? 3 : 1
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: Backend.stageCards
                delegate: Kirigami.AbstractCard {
                    id: stageCard

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1 // equal columns

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: page.stageTitles[stageCard.modelData.stage]
                            font.capitalization: Font.AllUppercase
                            opacity: 0.7
                        }
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 2
                            text: stageCard.modelData.current
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: stageCard.modelData.lines
                            delegate: StatusLine {
                                required property var modelData
                                Layout.fillWidth: true
                                level: modelData.level
                                text: modelData.text
                            }
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        QQC2.Button {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Change…")
                            onClicked: page.openStage(stageCard.modelData.stage)
                        }
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            opacity: 0.7
            wrapMode: Text.Wrap
            text: qsTr("Initrds are built by %1.").arg(Backend.system.generator || qsTr("an unknown tool"))
        }
    }
}
