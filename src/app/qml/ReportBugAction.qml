import QtQuick
import org.kde.kirigami as Kirigami
import Ekpo

// The Report a Bug button in every page's header. Main.qml opens the dialog and owns
// the Ctrl+Shift+B shortcut (one per window: several would be ambiguous).
Kirigami.Action {
    text: qsTr("Report a Bug…")
    icon.name: "tools-report-bug"
    tooltip: qsTr("Report a Bug (Ctrl+Shift+B)")
    displayHint: Kirigami.DisplayHint.IconOnly | Kirigami.DisplayHint.KeepVisible
    onTriggered: Backend.bugReportRequested()
}
