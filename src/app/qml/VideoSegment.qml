pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtMultimedia
import org.kde.kirigami as Kirigami

// Picks the part of a video to use: start and length, on a range over the whole video
// or in seconds, with a silent preview that loops just that part, e.g. to find where
// a menu animation repeats. One recording can so give one clip per theme.
ColumnLayout {
    id: segment

    property url source
    property real duration // seconds
    property real maxLength: duration // e.g. what fits in the theme
    property real start: 0
    property real length: duration
    readonly property real end: start + length

    // A new video: everything, or the first maxLength seconds.
    function reset(url, seconds) {
        player.stop();
        duration = seconds;
        start = 0;
        length = Math.min(seconds, maxLength);
        source = url;
    }
    function setStart(value) {
        start = Math.max(0, Math.min(value, duration - 0.1));
        length = Math.min(length, duration - start, maxLength);
        player.position = Math.round(start * 1000); // the preview shows where it starts
    }
    function setLength(value) {
        length = Math.max(0.1, Math.min(value, duration - start, maxLength));
    }

    spacing: Kirigami.Units.smallSpacing

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: width * 9 / 16
        color: "black"

        VideoOutput {
            id: output
            anchors.fill: parent
        }
        // No audio output: the preview is silent, like the splash.
        MediaPlayer {
            id: player
            source: segment.source
            videoOutput: output
            loops: MediaPlayer.Infinite
            property bool firstFrame: false // shown for this video

            onSourceChanged: firstFrame = false
            // Shows the frame at the start right away, not black until Play. Once:
            // pausing reports the media as loaded again.
            onMediaStatusChanged: {
                if (mediaStatus === MediaPlayer.LoadedMedia && !firstFrame) {
                    firstFrame = true;
                    play();
                    pause();
                    position = Math.round(segment.start * 1000);
                }
            }
            // Back to the start at the end of the part: a turn later, as a seek
            // reports its position only once it is done.
            onPositionChanged: {
                if (playing && position >= segment.end * 1000 && !rewind.running)
                    rewind.start();
            }
        }
        Timer {
            id: rewind
            interval: 0
            onTriggered: player.position = Math.round(segment.start * 1000)
        }
    }

    QQC2.RangeSlider {
        objectName: "range"
        Layout.fillWidth: true
        from: 0
        to: Math.max(0.1, segment.duration)
        stepSize: 0.1
        first.value: segment.start
        second.value: segment.end
        first.onMoved: segment.setStart(first.value)
        second.onMoved: segment.setLength(second.value - segment.start)
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: qsTr("Start (s):")
        }
        DecimalField {
            objectName: "startField"
            from: 0
            to: Math.round(segment.duration * 100)
            number: segment.start
            onEdited: v => segment.setStart(v)
        }
        QQC2.Label {
            text: qsTr("Length (s):")
        }
        DecimalField {
            objectName: "lengthField"
            from: 10
            to: Math.round(Math.min(segment.duration, segment.maxLength) * 100)
            number: segment.length
            onEdited: v => segment.setLength(v)
        }
        Item {
            Layout.fillWidth: true
        }
        QQC2.ToolButton {
            icon.name: player.playing ? "media-playback-pause" : "media-playback-start"
            text: player.playing ? qsTr("Pause") : qsTr("Play the Part")
            onClicked: {
                if (player.playing) {
                    player.pause();
                } else {
                    player.position = Math.round(segment.start * 1000);
                    player.play();
                }
            }
        }
    }
}
