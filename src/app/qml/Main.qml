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
    property var currentSection: mainSections[0]

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

    // ponytail: one placeholder page for every section until real pages exist.
    pageStack.initialPage: Kirigami.Page {
        title: root.currentSection.title

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            text: qsTr("Not built yet")
            explanation: qsTr("This page arrives in a later M1 step.")
        }
    }
}
