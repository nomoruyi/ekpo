import QtQuick
import QtTest
import "../../src/app/qml"

// A styled boot splash bar is put together from the script module's images, as
// splash.script puts them together.
TestCase {
    id: test

    readonly property url image: Qt.resolvedUrl("../fixtures/assets/logo.png") // 8 × 8

    function bar(style) {
        return { x: 0, y: 0, width: 200, height: 8, style: style, caps: 4, margin: style === "glow" ? 8 : 0, segments: 10, segmentWidth: 8, gap: 4,
                 box: image, left: image, middle: image, right: image, segment: image, color: "red", background: "blue" };
    }

    name: "PlymouthPreview"
    when: windowShown
    visible: true
    width: 400
    height: 300

    PlymouthPreview {
        id: preview
        width: 400
        height: 300
        layout: ({ module: "script", builtWidth: 400, builtHeight: 300, hideMessages: true })
    }

    function test_segmentsFollowTheProgress() {
        preview.layout = { module: "script", builtWidth: 400, builtHeight: 300, hideMessages: true, progress: bar("segmented") };
        preview.progress = 0.35;
        const segments = findChild(preview, "segments");
        verify(segments);
        compare(segments.count, 10);
        let visible = 0;
        for (let i = 0; i < segments.count; ++i)
            visible += segments.itemAt(i).visible ? 1 : 0;
        compare(visible, 3);
        compare(segments.itemAt(2).x, 24);
    }

    function test_endsAndStretchedMiddle() {
        for (const style of ["rounded", "glow"]) {
            preview.layout = { module: "script", builtWidth: 400, builtHeight: 300, hideMessages: true, progress: bar(style) };
            preview.progress = 0.5;
            const fill = findChild(preview, "fill");
            verify(fill.visible, style);
            compare(fill.x, style === "glow" ? -8 : 0);
            compare(fill.children[1].width, 100 - 2 * 4); // the middle, between the ends
            preview.progress = 0.02; // 4 px: less than both ends
            verify(!fill.visible, style);
        }
    }
}
