pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// The asset library (SPEC.md §4.13): images, videos and fonts stored once, shared
// palettes and logos, what each is used by, and clean-up. Something in use can't be
// moved to the trash: a design, saved version or draft would lose it.
Kirigami.ScrollablePage {
    id: page

    property string filter: "" // a kind, or "" for all
    property string message: ""
    readonly property var kinds: [{ kind: "", title: qsTr("All") }, { kind: "image", title: qsTr("Images") }, { kind: "video", title: qsTr("Videos") },
        { kind: "font", title: qsTr("Fonts") }, { kind: "palette", title: qsTr("Palettes") }, { kind: "logo", title: qsTr("Logos") }]
    readonly property var shown: Backend.assetList.filter(a => filter === "" || a.kind === filter)

    function kindName(kind) {
        return ({ image: qsTr("Image"), video: qsTr("Video"), font: qsTr("Font"), palette: qsTr("Palette"), logo: qsTr("Shared logo") })[kind] ?? kind;
    }

    title: qsTr("Assets")
    onVisibleChanged: if (visible) Backend.reloadAssets()
    actions: [
        Kirigami.Action {
            text: qsTr("Add Files…")
            icon.name: "list-add"
            onTriggered: addDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Palette from Image…")
            icon.name: "color-management"
            onTriggered: paletteDialog.open()
        },
        Kirigami.Action {
            text: qsTr("Remove Unused…")
            icon.name: "edit-clear-all"
            enabled: Backend.assetList.some(a => a.uses.length === 0)
            onTriggered: cleanupDialog.open()
        },
        ReportBugAction {}
    ]

    header: QQC2.TabBar {
        id: tabs
        Repeater {
            model: page.kinds
            delegate: QQC2.TabButton {
                required property var modelData
                text: modelData.title
                onClicked: page.filter = modelData.kind
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.message !== ""
            type: Kirigami.MessageType.Error
            showCloseButton: true
            text: page.message
            onVisibleChanged: if (!visible) page.message = ""
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 4
            visible: page.shown.length === 0
            icon.name: "folder-pictures"
            text: page.filter === "" ? qsTr("No assets yet") : qsTr("None of these yet")
            explanation: qsTr("Images, videos and fonts you use in designs are stored here once, with where they came from. Palettes and logos you share between designs are here too.")
        }

        Kirigami.CardsLayout {
            Layout.fillWidth: true
            visible: page.shown.length > 0

            Repeater {
                model: page.shown
                delegate: Kirigami.Card {
                    id: card

                    required property var modelData
                    readonly property bool used: modelData.uses.length > 0

                    banner.title: modelData.name
                    // (Lists, not hidden actions: Kirigami 6.24 can't lay out hidden actions.)
                    actions: modelData.kind === "font" ? [renameAction, licenceAction, trashAction] : [renameAction, trashAction]

                    Kirigami.Action {
                        id: renameAction
                        text: qsTr("Rename…")
                        icon.name: "edit-rename"
                        onTriggered: {
                            renameDialog.key = card.modelData.key;
                            renameField.text = card.modelData.name;
                            renameDialog.open();
                        }
                    }
                    Kirigami.Action {
                        id: licenceAction
                        text: qsTr("Licence")
                        icon.name: "license"
                        enabled: card.modelData.licenceUrl?.toString() !== ""
                        onTriggered: Qt.openUrlExternally(card.modelData.licenceUrl)
                    }
                    Kirigami.Action {
                        id: trashAction
                        text: qsTr("Move to Trash")
                        icon.name: "edit-delete"
                        enabled: !card.used
                        tooltip: card.used ? qsTr("Used by %1").arg(card.modelData.usedBy.join(", ")) : ""
                        onTriggered: page.message = Backend.removeItem(card.modelData.key)
                    }

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        // What it looks like.
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 5

                            Image {
                                anchors.fill: parent
                                visible: card.modelData.kind === "image" || card.modelData.kind === "logo"
                                source: visible ? card.modelData.url : ""
                                sourceSize.height: Kirigami.Units.gridUnit * 10
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: Kirigami.Units.iconSizes.huge
                                height: width
                                visible: card.modelData.kind === "video"
                                source: "video-x-generic"
                            }
                            QQC2.Label {
                                anchors.centerIn: parent
                                visible: card.modelData.kind === "font"
                                text: "Aa Bb 123"
                                font.family: fontLoader.status === FontLoader.Ready ? fontLoader.font.family : Kirigami.Theme.defaultFont.family
                                font.pixelSize: Kirigami.Units.gridUnit * 2
                                FontLoader {
                                    id: fontLoader
                                    source: card.modelData.kind === "font" ? card.modelData.url : ""
                                }
                            }
                            Row {
                                anchors.centerIn: parent
                                visible: card.modelData.kind === "palette"
                                spacing: Kirigami.Units.smallSpacing
                                Repeater {
                                    model: card.modelData.colours ?? []
                                    delegate: Rectangle {
                                        required property string modelData
                                        width: Kirigami.Units.gridUnit * 2
                                        height: width
                                        radius: Kirigami.Units.cornerRadius
                                        color: modelData
                                        border.color: Kirigami.Theme.disabledTextColor
                                    }
                                }
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            opacity: 0.7
                            text: [page.kindName(card.modelData.kind), card.modelData.size, card.modelData.added].filter(s => s).join(" · ")
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: card.used ? qsTr("Used by %1").arg(card.modelData.usedBy.join(", ")) : qsTr("Not used")
                            QQC2.ToolTip.text: card.modelData.uses.join("\n")
                            QQC2.ToolTip.visible: card.used && hoverHandler.hovered
                            HoverHandler {
                                id: hoverHandler
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: (card.modelData.source ?? "") !== ""
                            elide: Text.ElideMiddle
                            opacity: 0.7
                            font: Kirigami.Theme.smallFont
                            text: qsTr("From %1").arg(card.modelData.source)
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: paletteDialog
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.svg)")]
        onAccepted: page.message = Backend.paletteFromImage(selectedFile)
    }
    FileDialog {
        id: addDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images and fonts (*.png *.jpg *.jpeg *.webp *.svg *.ttf *.otf *.ttc)")]
        onAccepted: page.message = Backend.addAssetFiles(selectedFiles).errors.join("\n")
    }

    Kirigami.PromptDialog {
        id: renameDialog

        property string key

        title: qsTr("Rename")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: page.message = Backend.renameItem(key, renameField.text)

        QQC2.TextField {
            id: renameField
            onAccepted: renameDialog.accept()
        }
    }

    Kirigami.PromptDialog {
        id: cleanupDialog

        title: qsTr("Remove Unused Assets?")
        subtitle: qsTr("%n item(s) that no design, saved version, draft, shared logo or font uses move to the trash.", "",
                       Backend.assetList.filter(a => a.uses.length === 0).length)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Move to Trash")
                icon.name: "edit-delete"
                onTriggered: {
                    cleanupDialog.close();
                    page.message = Backend.removeUnused().errors.join("\n");
                }
            }
        ]
    }
}
