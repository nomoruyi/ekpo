import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One finding with an icon for its level: ok, info, warning, error.
RowLayout {
    id: statusLine

    required property string level
    required property string text

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Icon {
        Layout.alignment: Qt.AlignTop
        implicitWidth: Kirigami.Units.iconSizes.small
        implicitHeight: Kirigami.Units.iconSizes.small
        source: ({ ok: "dialog-ok-apply", info: "dialog-information", warning: "dialog-warning", error: "dialog-error" })[statusLine.level] ?? "dialog-information"
    }
    QQC2.Label {
        Layout.fillWidth: true
        text: statusLine.text
        wrapMode: Text.Wrap
    }
}
