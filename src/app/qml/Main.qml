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
    property string startEdit: "" // EKPO_EDIT_DESIGN (dev only): open this design in the editor
    property bool startBugReport: false // EKPO_BUG_REPORT (dev only): open Report a Bug
    property bool startUpdateDialog: false // EKPO_UPDATE_DIALOG (dev only): open the update dialog once a check finds one
    property bool startTour: false // EKPO_TOUR (dev only): switch pages as clicks would, see the tour below
    property bool tourDone: false // the screenshot follows it
    property bool editing: false // the editor has the window; the sidebar is hidden
    property var editorState: ({}) // the editor page's selection and preview size while its review shows
    property var currentSection: allSections.find(s => s.name === startSection) ?? mainSections[0]

    function showSection(name) {
        currentSection = allSections.find(s => s.name === name);
    }
    function pageFor(name) {
        return ({ overview: overviewPage, bootMenu: bootMenuPage, bootSplash: bootSplashPage, plasmaSplash: plasmaSplashPage, masks: masksPage, history: historyPage, settings: settingsPage, about: aboutPage })[name] ?? placeholderPage;
    }
    function showPage(page) {
        const old = pageStack.currentItem;
        if (old === page)
            return;
        pageStack.replace(page);
        if (old instanceof EditorPage)
            old.destroy();
    }
    // The review takes over the window until it is closed (SPEC.md §4.1).
    // (Not a Kirigami layer: pushing any page onto layers warns in Kirigami 6.24.)
    function showReview() {
        reviewPage.reset();
        showPage(reviewPage);
    }

    // The editor opens in the window with the sidebar collapsed (SPEC.md §4.4).
    // (Set, not bound: Kirigami opens a non-modal drawer itself at start.)
    function showEditor(state) {
        editing = true;
        globalDrawer.drawerOpen = false;
        showPage(editorComponent.createObject(contentItem, Object.assign({ visible: false }, state)));
    }

    // Report a Bug from any page: the sidebar, each page's header and Ctrl+Shift+B. The
    // screenshot is taken before the dialog opens, so it shows the page as it was.
    function reportBug() {
        if (!bugReportDialog.opened)
            bugReportDialog.start(Backend.captureScreen(root), screenInfo());
    }
    function screenInfo() {
        const lines = [];
        if (pageStack.currentItem === reviewPage)
            lines.push(qsTr("Review: %1").arg(Backend.plan.title ?? ""));
        else if (editing)
            // (The name last: a later arg() would replace a "%n" in it.)
            lines.push(qsTr("Editor: %1 design “%5”, id %2, version %3%4").arg(Backend.editor.stage).arg(Backend.editor.designId)
                       .arg(Backend.editor.version).arg(Backend.editor.dirty ? qsTr(", changed") : "").arg(Backend.editor.name));
        else
            lines.push(qsTr("Page: %1").arg(currentSection.title));
        if (Backend.applying)
            lines.push(qsTr("An apply was running."));
        lines.push(qsTr("Window: %1 × %2").arg(width).arg(height));
        return lines.join("\n");
    }
    Shortcut {
        sequence: "Ctrl+Shift+B"
        onActivated: root.reportBug()
    }
    BugReportDialog {
        id: bugReportDialog
    }
    UpdateDialog {
        id: updateDialog
    }
    // (Opened once the window is laid out, like Report a Bug's: Kirigami centres
    // dialogs on the window's size.)
    Connections {
        target: Backend
        enabled: root.startUpdateDialog
        function onUpdateChanged() {
            if (Backend.updateInfo.available === true) {
                root.startUpdateDialog = false;
                updateDialogTimer.start();
            }
        }
    }
    Timer {
        id: updateDialogTimer
        interval: 300
        onTriggered: updateDialog.open()
    }
    Timer {
        running: root.startBugReport
        interval: 300
        onTriggered: root.reportBug()
    }
    // The tour: every section in turn, then with EKPO_EDIT_DESIGN the editor opens and
    // closes twice (tests run it with fatal warnings). The editor gets longer: leaving it
    // while Kirigami still builds its toolbar warns, and CI machines are slow.
    Timer {
        property int step: 0
        running: root.startTour
        interval: step < root.allSections.length ? 500 : 1500
        repeat: true
        onTriggered: {
            const sections = root.allSections.length;
            if (step < sections) {
                root.currentSection = root.allSections[step];
            } else if (root.startEdit === "" || step >= sections + 4) {
                stop();
                root.tourDone = true;
            } else if ((step - sections) % 2 === 0) {
                Backend.editor.open(root.startEdit);
                root.showEditor();
            } else {
                (root.pageStack.currentItem as EditorPage)?.tryLeave();
            }
            ++step;
        }
    }

    onCurrentSectionChanged: {
        const page = pageFor(currentSection.name);
        if (pageStack.depth > 0 && !Backend.applying)
            showPage(page);
    }
    // Pages are kept as items: pushing components makes Kirigami warn, and the
    // pages keep their state (e.g. the last import message) between visits.
    pageStack.initialPage: pageFor(currentSection.name)
    // Tell the user when an apply ends while the window is in the background.
    Connections {
        target: Backend
        function onBugReportRequested() {
            root.reportBug();
        }
        function onApplyFinished(ok, error, number) {
            if (!root.active)
                Backend.notify(ok ? qsTr("Done") : qsTr("Nothing was changed"),
                               ok ? (number > 0 ? qsTr("Saved as history entry #%1.").arg(number) : qsTr("Finished.")) : error);
        }
    }

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
        running: !root.startDialogsShown && root.startReview === "" && root.startEdit === "" && !root.startBugReport
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
        running: root.startEdit !== "" && !root.startTour
        interval: 300
        onTriggered: {
            Backend.editor.open(root.startEdit);
            root.showEditor();
        }
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
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                text: qsTr("Report a Bug…")
                icon.name: "tools-report-bug"
                onClicked: root.reportBug()
            }
            Repeater {
                model: root.bottomSections
                delegate: SidebarItem {}
            }
            QQC2.Label {
                Layout.fillWidth: true
                leftPadding: Kirigami.Units.largeSpacing
                bottomPadding: Kirigami.Units.smallSpacing
                opacity: 0.6
                font: Kirigami.Theme.smallFont
                text: qsTr("Version %1").arg(Qt.application.version)
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
        onUpdateRequested: updateDialog.open()
    }
    AboutPage {
        id: aboutPage
        visible: false
        onIntro: introDialog.open()
    }
    SettingsPage {
        id: settingsPage
        visible: false
        onReview: root.showReview()
        onUpdateRequested: updateDialog.open()
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
        onDone: root.editing ? root.showEditor(root.editorState) : root.showPage(root.pageFor(root.currentSection.name))
    }
    StagePage {
        id: bootMenuPage
        onReview: root.showReview()
        onEdit: root.showEditor()
        visible: false
        stage: "bootMenu"
        title: qsTr("Boot Menu")
    }
    StagePage {
        id: bootSplashPage
        onReview: root.showReview()
        onEdit: root.showEditor()
        visible: false
        stage: "bootSplash"
        title: qsTr("Boot Splash")
    }
    StagePage {
        id: plasmaSplashPage
        onReview: root.showReview()
        onEdit: root.showEditor()
        visible: false
        stage: "plasmaSplash"
        title: qsTr("Plasma Splash")
    }

    // A new editor page each time the editor shows (its state lives in Backend.editor),
    // destroyed when it leaves: Kirigami 6.24 loses the page row of an editor page that
    // comes back into the row, and its header warns while it rebuilds the toolbar.
    Component {
        id: editorComponent

        EditorPage {
            id: editorItem

            onReview: {
                root.editorState = { selected: editorItem.selected, resolution: editorItem.resolution };
                root.showReview();
            }
            onLeave: {
                root.editing = false;
                root.globalDrawer.drawerOpen = true;
                root.showPage(root.pageFor(root.currentSection.name));
            }
        }
    }

    // ponytail: one placeholder for the sections later steps build.
    Kirigami.Page {
        id: placeholderPage
        visible: false
        title: root.currentSection.title
        actions: [
            ReportBugAction {}
        ]

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            text: qsTr("Not built yet")
            explanation: qsTr("This page arrives in a later M1 step.")
        }
    }
}
