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
        visible: preview.bar !== null
        width: preview.bar?.width ?? 0
        height: preview.bar?.height ?? 0
        // two-step: alignment × (screen − bar), the alignment taken from the built
        // screen; the script module: the same fraction of the screen.
        x: preview.layout.module === "script" ? (preview.bar?.x ?? 0) / preview.layout.builtWidth * preview.width
                                              : (preview.bar?.x ?? 0) / Math.max(1, preview.layout.builtWidth - width) * (preview.width - width)
        y: preview.layout.module === "script" ? (preview.bar?.y ?? 0) / preview.layout.builtHeight * preview.height
                                              : (preview.bar?.y ?? 0) / Math.max(1, preview.layout.builtHeight - height) * (preview.height - height)

        Rectangle {
            anchors.fill: parent
            color: preview.bar?.background ?? "transparent"
        }
        Rectangle {
            width: parent.width * preview.progress
            height: parent.height
            color: preview.bar?.color ?? "transparent"
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
