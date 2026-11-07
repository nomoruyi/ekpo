import QtQuick
import QtTest
import "../../src/app/qml"

// The editor's colour picker (issue #4: Qt's dialog showed a blank square).
TestCase {
    id: test

    name: "ColorPicker"
    when: windowShown
    visible: true
    width: 400
    height: 400

    ColorPicker {
        id: picker
        width: 360
    }

    function near(a, b) {
        return Math.abs(a.r - b.r) < 0.01 && Math.abs(a.g - b.g) < 0.01 && Math.abs(a.b - b.b) < 0.01 && Math.abs(a.a - b.a) < 0.01;
    }

    function init() {
        picker.load("#442c5e");
    }

    function test_loadsAndKeepsColour() {
        verify(near(picker.picked, Qt.color("#442c5e")), picker.picked);
        compare(picker.original, Qt.color("#442c5e"));
    }

    function test_drawsTheSquare() {
        const square = findChild(picker, "square");
        waitForRendering(square);
        const image = grabImage(square);
        const w = image.width, h = image.height;
        const topRight = Qt.color(image.pixel(w - 3, 3));
        const topLeft = Qt.color(image.pixel(3, 3));
        const bottom = Qt.color(image.pixel(w / 2, h - 3));
        verify(topLeft.r > 0.95 && topLeft.g > 0.95 && topLeft.b > 0.95, "top left is white: " + topLeft);
        verify(near(topRight, Qt.hsva(picker.hue, 1, 1, 1)) || topRight.hsvSaturation > 0.9, "top right is the pure hue: " + topRight);
        verify(bottom.hsvValue < 0.1, "the bottom is black: " + bottom);
        const middle = Qt.color(image.pixel(w / 2, h / 2));
        verify(middle.hsvSaturation > 0.2 && middle.hsvValue < 0.9, "not blank in the middle: " + middle);
    }

    function test_picksInTheSquare() {
        const square = findChild(picker, "square");
        mouseClick(square, 1, 1); // top left
        verify(near(picker.picked, Qt.color("white")), picker.picked);
        mouseClick(square, square.width - 1, square.height - 1); // bottom right
        compare(picker.brightness, 1 - (square.height - 1) / square.height);
        compare(picker.original, Qt.color("#442c5e")); // "before" stays
    }

    function test_takesHex() {
        const hex = findChild(picker, "hexField");
        hex.forceActiveFocus();
        hex.selectAll();
        keyClick(Qt.Key_Delete);
        for (const c of "#80ff0000")
            keyClick(c);
        keyClick(Qt.Key_Return);
        verify(near(picker.picked, Qt.rgba(1, 0, 0, 128 / 255)), picker.picked);
        compare(hex.text, "#80ff0000");
        compare(picker.original, Qt.color("#442c5e"));
    }

    function test_movesHue() {
        const bar = findChild(picker, "hueBar");
        mouseClick(bar, bar.width / 3, bar.height / 2); // green
        verify(picker.picked.g > picker.picked.r && picker.picked.g > picker.picked.b, picker.picked);
    }
}
