pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Picks a colour: saturation and brightness in the square, hue and opacity in the bars
// below, or a hex code. Drawn with plain gradients: Qt's own colour dialog showed a
// blank square in the KDE style (issue #4). load() sets the starting colour; picked is
// the result.
ColumnLayout {
    id: picker

    property bool showAlpha: true
    property real hue: 0
    property real saturation: 0
    property real brightness: 1
    property real alpha: 1
    property color original: "white"
    readonly property color picked: Qt.hsva(hue, saturation, brightness, alpha)

    function load(c: color) {
        original = c;
        set(c);
    }
    function set(c: color) {
        if (c.hsvHue >= 0) // greys have no hue: keep the last one
            hue = c.hsvHue;
        saturation = c.hsvSaturation;
        brightness = c.hsvValue;
        alpha = showAlpha ? c.a : 1;
    }
    function clamp(v) {
        return Math.max(0, Math.min(1, v));
    }

    spacing: Kirigami.Units.largeSpacing

    // Saturation from left to right, brightness from bottom to top.
    Item {
        id: square
        objectName: "square"
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 10

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 1; color: Qt.hsva(picker.hue, 1, 1, 1) }
            }
        }
        Rectangle {
            anchors.fill: parent
            border.color: Kirigami.Theme.disabledTextColor
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: "black" }
            }
        }
        Rectangle {
            x: picker.saturation * square.width - width / 2
            y: (1 - picker.brightness) * square.height - height / 2
            width: Kirigami.Units.gridUnit
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: picker.brightness > 0.6 && picker.saturation < 0.4 ? "black" : "white"
        }
        MouseArea {
            anchors.fill: parent
            function pick(mouse) {
                picker.saturation = picker.clamp(mouse.x / width);
                picker.brightness = 1 - picker.clamp(mouse.y / height);
            }
            onPressed: mouse => pick(mouse)
            onPositionChanged: mouse => pick(mouse)
        }
    }

    // A bar with a draggable marker; value 0–1.
    component Bar: Item {
        id: bar

        property real value
        property alias gradient: fill.gradient
        signal moved(real value)

        Layout.fillWidth: true
        implicitHeight: Kirigami.Units.gridUnit * 1.2

        Rectangle {
            id: fill
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            border.color: Kirigami.Theme.disabledTextColor
        }
        Rectangle {
            x: bar.value * bar.width - width / 2
            y: -2
            width: 6
            height: bar.height + 4
            radius: 2
            color: "white"
            border.color: "black"
        }
        MouseArea {
            anchors.fill: parent
            onPressed: mouse => bar.moved(picker.clamp(mouse.x / width))
            onPositionChanged: mouse => bar.moved(picker.clamp(mouse.x / width))
        }
    }

    Bar {
        objectName: "hueBar"
        value: picker.hue
        onMoved: v => picker.hue = v
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0 / 6; color: "#ff0000" }
            GradientStop { position: 1 / 6; color: "#ffff00" }
            GradientStop { position: 2 / 6; color: "#00ff00" }
            GradientStop { position: 3 / 6; color: "#00ffff" }
            GradientStop { position: 4 / 6; color: "#0000ff" }
            GradientStop { position: 5 / 6; color: "#ff00ff" }
            GradientStop { position: 1; color: "#ff0000" }
        }
    }
    Bar {
        objectName: "alphaBar"
        visible: picker.showAlpha
        value: picker.alpha
        onMoved: v => picker.alpha = v
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.hsva(picker.hue, picker.saturation, picker.brightness, 0) }
            GradientStop { position: 1; color: Qt.hsva(picker.hue, picker.saturation, picker.brightness, 1) }
        }
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        // Before and after.
        Rectangle {
            implicitWidth: Kirigami.Units.gridUnit * 2
            implicitHeight: Kirigami.Units.gridUnit * 1.5
            color: picker.original
            border.color: Kirigami.Theme.disabledTextColor
        }
        Rectangle {
            implicitWidth: Kirigami.Units.gridUnit * 2
            implicitHeight: Kirigami.Units.gridUnit * 1.5
            color: picker.picked
            border.color: Kirigami.Theme.disabledTextColor
        }
        Item {
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: qsTr("Hex:")
        }
        // #RRGGBB, or #AARRGGBB with opacity (Qt's order).
        QQC2.TextField {
            objectName: "hexField"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
            font.family: "monospace"
            text: picker.picked.toString()
            validator: RegularExpressionValidator {
                regularExpression: /#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})/
            }
            onEditingFinished: {
                if (acceptableInput)
                    picker.set(text);
                text = Qt.binding(() => picker.picked.toString());
            }
        }
    }
}
