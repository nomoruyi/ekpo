pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// "Play the Boot" (SPEC.md §4.13): the boot menu, boot splash and Plasma splash
// previews one after another, each about as long as a boot shows it (playback.h).
QQC2.Popup {
    id: player

    property var steps: []
    property int current: 0
    property string title
    readonly property var step: steps[current] ?? ({})
    readonly property bool finished: current >= steps.length - 1 && !stepTimer.running
    readonly property size screenSize: Qt.size(steps[0]?.width ?? 2560, steps[0]?.height ?? 1440) // the screen the boot menu was drawn for

    function play(list, name) {
        steps = list;
        title = name;
        current = 0;
        if (steps.length > 0) {
            open();
            stepTimer.restart();
        }
    }
    function next() {
        if (current < steps.length - 1) {
            current++;
            stepTimer.restart();
        } else {
            stepTimer.stop();
        }
    }
    function stageName(stage) {
        return ({ bootMenu: qsTr("Boot menu"), bootSplash: qsTr("Boot splash"), plasmaSplash: qsTr("Plasma splash") })[stage] ?? stage;
    }

    parent: QQC2.Overlay.overlay // over the whole window, the sidebar too
    modal: true
    closePolicy: QQC2.Popup.CloseOnEscape
    padding: 0
    x: 0
    y: 0
    width: parent?.width ?? 0
    height: parent?.height ?? 0
    background: Rectangle {
        color: "black"
    }
    onClosed: {
        stepTimer.stop();
        steps = [];
    }

    Timer {
        id: stepTimer
        interval: (player.step.seconds ?? 5) * 1000
        onTriggered: player.next()
    }

    contentItem: Item {
        // The screen, fitted into the window.
        Item {
            id: screen

            readonly property real fit: Math.min(parent.width / player.screenSize.width, parent.height / player.screenSize.height)

            anchors.centerIn: parent
            width: player.screenSize.width * fit
            height: player.screenSize.height * fit
            clip: true

            Loader {
                // A new item per step, so animations start from the beginning.
                active: player.opened && player.steps.length > 0
                width: player.screenSize.width
                height: player.screenSize.height
                scale: screen.fit
                transformOrigin: Item.TopLeft
                sourceComponent: ({ image: menuView, plymouth: splashView, qml: plasmaView })[player.step.kind] ?? noneView
            }
        }

        // What plays, and the way out.
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Kirigami.Units.largeSpacing
            opacity: hover.hovered || player.finished ? 1 : 0.4

            Behavior on opacity {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                }
            }
            HoverHandler {
                id: hover
            }
            QQC2.Label {
                Layout.fillWidth: true
                color: "white"
                elide: Text.ElideRight
                text: qsTr("%1 · %2 (%3 of %4)").arg(player.stageName(player.step.stage)).arg(player.step.label ?? "").arg(player.current + 1).arg(player.steps.length)
            }
            QQC2.Button {
                text: qsTr("Next")
                icon.name: "media-skip-forward"
                visible: !player.finished
                onClicked: player.next()
            }
            QQC2.Button {
                text: qsTr("Play Again")
                icon.name: "media-playlist-repeat"
                onClicked: player.play(player.steps, player.title)
            }
            QQC2.Button {
                text: qsTr("Close")
                icon.name: "window-close"
                onClicked: player.close()
            }
        }
    }

    Component {
        id: menuView
        Image {
            source: player.step.source ?? ""
            fillMode: Image.Stretch
            cache: false
        }
    }
    Component {
        id: splashView
        PlymouthPreview {
            layout: player.step.layout ?? ({})
        }
    }
    Component {
        id: plasmaView
        Loader {
            id: plasma

            property int stage: 1 // ksplashqml's: it rises as Plasma starts, up to 6

            source: player.step.source ?? ""
            onLoaded: item.stage = Qt.binding(() => plasma.stage)
            Timer {
                running: plasma.status === Loader.Ready
                repeat: true
                interval: (player.step.seconds ?? 5) * 1000 / 6
                onTriggered: plasma.stage = Math.min(6, plasma.stage + 1)
            }
        }
    }
    Component {
        id: noneView
        Rectangle {
            color: "black"
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width * 0.6
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    color: "white"
                    font.pixelSize: 64
                    font.bold: true
                    text: player.step.label ?? ""
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: "#b0b0b0"
                    font.pixelSize: 36
                    text: player.step.note ?? ""
                }
            }
        }
    }
}
