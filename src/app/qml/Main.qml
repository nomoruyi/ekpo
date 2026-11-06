pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

Kirigami.ApplicationWindow {
    id: root

    width: Kirigami.Units.gridUnit * 60
    height: Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 26

    // Sidebar per SPEC.md §4.1; Settings and About sit at the bottom.
    readonly property var mainSections: [
        { name: "overview", title: qsTr("Overview"), icon: "dashboard-show" },
        { name: "bootMenu", title: qsTr("Boot Menu"), icon: "view-list-details" },
        { name: "bootSplash", title: qsTr("Boot Splash"), icon: "preferences-system-startup" },
        { name: "plasmaSplash", title: qsTr("Plasma Splash"), icon: "plasma" },
        { name: "masks", title: qsTr("Masks"), icon: "user-identity" },
        { name: "assets", title: qsTr("Assets"), icon: "folder-pictures" },
        { name: "history", title: qsTr("History"), icon: "view-history" }
    ]
    readonly property var bottomSections: [
        { name: "settings", title: qsTr("Settings"), icon: "settings-configure" },
        { name: "about", title: qsTr("About"), icon: "help-about" }
    ]
    readonly property var allSections: mainSections.concat(bottomSections)
    property string startSection: "overview" // EKPO_SECTION, for screenshots
    property string startReview: "" // EKPO_REVIEW_IMPORT (dev only): open the review of this import
    property var currentSection: allSections.find(s => s.name === startSection) ?? mainSections[0]

    function showSection(name) {
        currentSection = allSections.find(s => s.name === name);
    }
    function pageFor(name) {
        return ({ overview: overviewPage, bootMenu: bootMenuPage, bootSplash: bootSplashPage, plasmaSplash: plasmaSplashPage, masks: masksPage, history: historyPage })[name] ?? placeholderPage;
    }
    // The review takes over the window until it is closed (SPEC.md §4.1).
    // (Not a Kirigami layer: pushing any page onto layers warns in Kirigami 6.24.)
    function showReview() {
        reviewPage.reset();
        if (pageStack.currentItem !== reviewPage)
            pageStack.replace(reviewPage);
    }

    onCurrentSectionChanged: {
        const page = pageFor(currentSection.name);
        if (pageStack.depth > 0 && pageStack.currentItem !== page && !Backend.applying)
            pageStack.replace(page);
    }
    // Pages are kept as items: pushing components makes Kirigami warn, and the
    // pages keep their state (e.g. the last import message) between visits.
    pageStack.initialPage: pageFor(currentSection.name)
    // Checks for outside changes whenever the window comes back (SPEC.md §4.12).
    onActiveChanged: {
        if (active && !Backend.applying && pageStack.currentItem !== reviewPage)
            Backend.refresh();
    }

    // After a reboot: did the last boot-stage apply work? (SPEC.md §4.9)
    Kirigami.PromptDialog {
        id: checkDialog

        title: qsTr("Did the New Look Work?")
        subtitle: qsTr("You applied \u201c%1\u201d before restarting. If the boot menu or splash looked wrong, Ekpo can put back what was there before.")
            .arg(Backend.pendingCheck.title ?? "")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Keep It")
                icon.name: "dialog-ok-apply"
                onTriggered: {
                    Backend.keepCheck();
                    checkDialog.close();
                }
            },
            Kirigami.Action {
                text: qsTr("Revert…")
                icon.name: "edit-undo"
                onTriggered: {
                    checkDialog.close();
                    Backend.planRevertCheck();
                    root.showReview();
                }
            }
        ]
    }
    // Start dialogs open once per run: their conditions are bound to Backend, which
    // emits changed() on every refresh and setting change, restarting a bound timer.
    property bool startDialogsShown: false
    Timer {
        running: !root.startDialogsShown && root.startReview === ""
        interval: 500
        onTriggered: {
            root.startDialogsShown = true;
            if (Backend.pendingCheck.number !== undefined)
                checkDialog.open();
            else if (Backend.showIntro)
                introDialog.open();
        }
    }

    IntroDialog {
        id: introDialog
    }

    Timer {
        running: root.startReview !== ""
        interval: 300
        onTriggered: {
            Backend.planApplyImport(root.startReview);
            root.showReview();
        }
    }

    globalDrawer: Kirigami.OverlayDrawer {
        edge: Qt.LeftEdge
        modal: false
        handleVisible: false
        enabled: !Backend.applying
        width: Kirigami.Units.gridUnit * 12

        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0

        contentItem: ColumnLayout {
            spacing: 0

            Repeater {
                model: root.mainSections
                delegate: SidebarItem {}
            }
            Item {
                Layout.fillHeight: true
            }
            Kirigami.Separator {
                Layout.fillWidth: true
            }
            Repeater {
                model: root.bottomSections
                delegate: SidebarItem {}
            }
        }
    }

    component SidebarItem: QQC2.ItemDelegate {
        required property var modelData

        Layout.fillWidth: true
        text: modelData.title
        icon.name: modelData.icon
        highlighted: root.currentSection.name === modelData.name
        onClicked: root.currentSection = modelData
    }

    OverviewPage {
        id: overviewPage
        visible: false
        onOpenStage: stage => root.showSection(stage)
        onReview: root.showReview()
        onIntro: introDialog.open()
    }
    MasksPage {
        id: masksPage
        visible: false
        onReview: root.showReview()
    }
    HistoryPage {
        id: historyPage
        visible: false
        onReview: root.showReview()
    }
    ReviewPage {
        id: reviewPage
        visible: false
        onDone: root.pageStack.replace(root.pageFor(root.currentSection.name))
    }
    StagePage {
        id: bootMenuPage
        onReview: root.showReview()
        visible: false
        stage: "bootMenu"
        title: qsTr("Boot Menu")
    }
    StagePage {
        id: bootSplashPage
        onReview: root.showReview()
        visible: false
        stage: "bootSplash"
        title: qsTr("Boot Splash")
    }
    StagePage {
        id: plasmaSplashPage
        onReview: root.showReview()
        visible: false
        stage: "plasmaSplash"
        title: qsTr("Plasma Splash")
    }

    // ponytail: one placeholder for the sections later steps build.
    Kirigami.Page {
        id: placeholderPage
        visible: false
        title: root.currentSection.title

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            text: qsTr("Not built yet")
            explanation: qsTr("This page arrives in a later M1 step.")
        }
    }
}
