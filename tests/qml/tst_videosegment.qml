import QtQuick
import QtTest
import "../../src/app/qml"

// The part of a video to use: it stays inside the video and within maxLength.
TestCase {
    id: test

    readonly property url clip: Qt.resolvedUrl("../fixtures/assets/clip.mp4")

    name: "VideoSegment"
    when: windowShown
    visible: true
    width: 420
    height: 420

    VideoSegment {
        id: segment
        width: 400
    }

    function init() {
        segment.maxLength = 100;
        segment.reset(test.clip, 10);
    }

    function test_resetTakesAllOrTheMaximum() {
        compare(segment.start, 0);
        compare(segment.length, 10);
        segment.maxLength = 2;
        segment.reset(test.clip, 10);
        compare(segment.length, 2);
    }

    function test_staysInsideTheVideo() {
        segment.setStart(8);
        compare(segment.start, 8);
        compare(segment.length, 2); // what is left after 8 s
        segment.setLength(5);
        compare(segment.length, 2);
        segment.setStart(12);
        compare(segment.start, 9.9);
        segment.setStart(1);
        segment.setLength(3);
        compare(segment.end, 4);
    }

    function test_typesTheStart() {
        const start = findChild(segment, "startField");
        start.contentItem.forceActiveFocus();
        start.contentItem.selectAll();
        keyClick(Qt.Key_2);
        keyClick(Qt.Key_Return);
        compare(segment.start, 2);
        compare(segment.length, 8);
    }

    // Loading shows the first frame without looping (warnings are fatal here).
    function test_loadsQuietly() {
        segment.reset(test.clip, 1);
        segment.setStart(0.2);
        wait(800);
        compare(segment.start, 0.2);
    }
}
