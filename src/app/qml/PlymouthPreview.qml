pragma ComponentBehavior: Bound

import QtQuick

// What Plymouth draws for a boot splash built by genplymouth (SPEC.md §4.6), on a
// screen of this item's size: background.png stretched to the screen, the animation
// and the progress bar at their own pixel size, a sample status message. Its files
// are the built theme's own.
Item {
    id: preview

    required property var layout // DesignEditor.plymouth
    readonly property var frames: layout.frames ?? []
    readonly property var bar: layout.progress ?? null
    property int frame: 0
    property real progress: 0

    clip: true

    Image {
        anchors.fill: parent
        source: preview.layout.background ?? ""
        fillMode: Image.Stretch // ScaleBackgroundImage / Image.Scale: no aspect ratio
        smooth: true
    }
    // Moving images, as splash.script moves them (gencommon.h).
    Repeater {
        model: preview.layout.images ?? []
        delegate: Image {
            id: moving

            required property var modelData
            readonly property string animation: modelData.animation

            source: modelData.source
            width: modelData.width
            height: modelData.height
            x: modelData.centreX * preview.width - width / 2
            y: modelData.centreY * preview.height - height / 2
            transform: Translate {
                id: slide
            }

            SequentialAnimation {
                running: moving.animation === "fade" || moving.animation === "slide"
                ParallelAnimation {
                    NumberAnimation {
                        target: moving
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: moving.animation === "fade" ? 1000 : 800
                    }
                    NumberAnimation {
                        target: slide
                        property: "y"
                        from: moving.animation === "slide" ? preview.height * 0.05 : 0
                        to: 0
                        duration: 800
                        easing.type: Easing.OutCubic
                    }
                }
            }
            SequentialAnimation {
                running: moving.animation === "breathe" || moving.animation === "pulse"
                loops: Animation.Infinite
                NumberAnimation {
                    target: moving
                    property: moving.animation === "pulse" ? "scale" : "opacity"
                    from: 1
                    to: moving.animation === "pulse" ? 0.92 : 0.4
                    duration: moving.animation === "pulse" ? 750 : 1500
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    target: moving
                    property: moving.animation === "pulse" ? "scale" : "opacity"
                    to: 1
                    duration: moving.animation === "pulse" ? 750 : 1500
                    easing.type: Easing.InOutSine
                }
            }
            RotationAnimator on rotation {
                running: moving.animation === "spin"
                from: 0
                to: 360
                duration: 4000
                loops: Animation.Infinite
            }
        }
    }
    Image {
        id: animation
        visible: preview.frames.length > 0
        source: preview.frames[Math.min(preview.frame, preview.frames.length - 1)] ?? ""
        x: preview.layout.centreX * preview.width - width / 2
        y: preview.layout.centreY * preview.height - height / 2
    }
    // two-step redraws at 30 fps and fits any number of frames into 2 s; the script
    // module redraws at 50 Hz.
    Timer {
        running: preview.visible && preview.frames.length > 1
        repeat: true
        interval: Math.max(preview.layout.module === "script" ? 20 : 33, preview.layout.seconds * 1000 / preview.frames.length)
        onTriggered: preview.frame = (preview.frame + 1) % preview.frames.length
    }
    Item {
        id: barItem

        visible: preview.bar !== null
        width: preview.bar?.width ?? 0
        height: preview.bar?.height ?? 0
        // two-step: alignment × (screen − bar), the alignment taken from the built
        // screen; the script module: the same fraction of the screen.
        x: preview.layout.module === "script" ? (preview.bar?.x ?? 0) / preview.layout.builtWidth * preview.width
                                              : (preview.bar?.x ?? 0) / Math.max(1, preview.layout.builtWidth - width) * (preview.width - width)
        y: preview.layout.module === "script" ? (preview.bar?.y ?? 0) / preview.layout.builtHeight * preview.height
                                              : (preview.bar?.y ?? 0) / Math.max(1, preview.layout.builtHeight - height) * (preview.height - height)

        readonly property string style: preview.bar?.style ?? "flat"
        readonly property int filled: Math.floor(width * preview.progress)

        Rectangle {
            visible: parent.style === "flat"
            anchors.fill: parent
            color: preview.bar?.background ?? "transparent"
        }
        Rectangle {
            visible: parent.style === "flat"
            width: parent.filled
            height: parent.height
            color: preview.bar?.color ?? "transparent"
        }
        // Styled: the script module's images, put together as splash.script does.
        Image {
            visible: parent.style !== "flat"
            source: visible ? preview.bar.box : ""
        }
        Repeater {
            objectName: "segments"
            model: parent.style === "segmented" ? preview.bar.segments : 0
            delegate: Image {
                required property int index
                visible: index < Math.floor(preview.bar.segments * preview.progress)
                x: index * (preview.bar.segmentWidth + preview.bar.gap)
                source: preview.bar.segment
            }
        }
        Row {
            objectName: "fill"
            readonly property bool shown: parent.style === "rounded" || parent.style === "glow"
            visible: shown && parent.filled > 2 * (preview.bar?.caps ?? 0)
            x: -(preview.bar?.margin ?? 0)
            y: -(preview.bar?.margin ?? 0)

            Image {
                source: parent.shown ? preview.bar.left : ""
            }
            Image {
                source: parent.shown ? preview.bar.middle : ""
                width: Math.max(1, barItem.filled - 2 * (preview.bar?.caps ?? 0))
                fillMode: Image.Stretch
            }
            Image {
                source: parent.shown ? preview.bar.right : ""
            }
        }
    }
    NumberAnimation on progress {
        running: preview.visible && preview.bar !== null
        from: 0
        to: 1
        duration: 6000
        loops: Animation.Infinite
    }
    // Live text is white Ubuntu under initramfs-tools (CLAUDE.md), below the animation.
    Text {
        visible: !(preview.layout.hideMessages ?? true)
        text: qsTr("Starting Network Manager…")
        color: "white"
        font.family: "Ubuntu"
        font.pixelSize: 16
        anchors.horizontalCenter: parent.horizontalCenter
        y: animation.visible ? animation.y + animation.height + 8 : parent.height * 0.75
    }
}
