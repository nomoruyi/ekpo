pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

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
        { name: "personas", title: qsTr("Personas"), icon: "user-identity" },
        { name: "assets", title: qsTr("Assets"), icon: "folder-pictures" },
        { name: "history", title: qsTr("History"), icon: "view-history" }
    ]
    readonly property var bottomSections: [
        { name: "settings", title: qsTr("Settings"), icon: "settings-configure" },
        { name: "about", title: qsTr("About"), icon: "help-about" }
    ]
    readonly property var allSections: mainSections.concat(bottomSections)
    property string startSection: "overview" // PERSONAE_SECTION, for screenshots
    property var currentSection: allSections.find(s => s.name === startSection) ?? mainSections[0]

    function showSection(name) {
        currentSection = allSections.find(s => s.name === name);
    }
    function pageFor(name) {
        return ({ overview: overviewPage, bootMenu: bootMenuPage, bootSplash: bootSplashPage, plasmaSplash: plasmaSplashPage })[name] ?? placeholderPage;
    }

    onCurrentSectionChanged: {
        const page = pageFor(currentSection.name);
        if (pageStack.depth > 0 && pageStack.currentItem !== page)
            pageStack.replace(page);
    }
    // Pages are kept as items: pushing components makes Kirigami warn, and the
    // pages keep their state (e.g. the last import message) between visits.
    pageStack.initialPage: pageFor(currentSection.name)

    globalDrawer: Kirigami.OverlayDrawer {
        edge: Qt.LeftEdge
        modal: false
        handleVisible: false
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
    }
    StagePage {
        id: bootMenuPage
        visible: false
        stage: "bootMenu"
        title: qsTr("Boot Menu")
    }
    StagePage {
        id: bootSplashPage
        visible: false
        stage: "bootSplash"
        title: qsTr("Boot Splash")
    }
    StagePage {
        id: plasmaSplashPage
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
