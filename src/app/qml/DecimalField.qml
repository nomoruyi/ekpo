import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

// A value with two decimals (e.g. % of the screen; the arrows step by 0.1), or as many
// as decimals says; from, to and stepSize count in steps of the last decimal.
//
// The KDE style's spin box takes every keystroke as the new value and assigns it from
// QML: "1" turned into "1.00" mid-typing, so typing 10 gave 1.000 (issue #1), and the
// assignment cut value loose from number. Its whole-number validator also refused the
// decimal point. This text field takes the number when editing finishes and reports
// it through edited(), so value keeps following number.
QQC2.SpinBox {
    id: field

    property real number
    property int decimals: 2
    readonly property real factor: Math.pow(10, decimals)
    signal edited(real value)

    from: -100000
    to: 100000
    stepSize: 10
    editable: true
    value: Math.round(number * factor)
    validator: DoubleValidator {
        bottom: Math.min(field.from, field.to) / field.factor
        top: Math.max(field.from, field.to) / field.factor
        decimals: field.decimals
        notation: DoubleValidator.StandardNotation
        locale: field.locale.name
    }
    function format(v: int, locale: var): string {
        return Number(v / factor).toLocaleString(locale, "f", decimals);
    }
    function parse(text: string, locale: var): int {
        const v = Number.fromLocaleString(locale, text);
        return isNaN(v) ? field.value : Math.round(v * factor);
    }
    textFromValue: (v, locale) => field.format(v, locale)
    valueFromText: (text, locale) => field.parse(text, locale)
    onValueModified: edited(value / factor)

    contentItem: T.TextField {
        implicitWidth: Math.max(widest.advanceWidth + 2, Math.round(contentWidth)) + leftPadding + rightPadding
        implicitHeight: Math.round(contentHeight) + topPadding + bottomPadding
        z: 2
        font: field.font
        color: Kirigami.Theme.textColor
        selectionColor: Kirigami.Theme.highlightColor
        selectedTextColor: Kirigami.Theme.highlightedTextColor
        selectByMouse: true
        hoverEnabled: false // hover belongs to the spin box
        verticalAlignment: Qt.AlignVCenter
        readOnly: !field.editable
        validator: field.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        text: field.displayText
        onEditingFinished: {
            const v = Math.max(field.from, Math.min(field.to, field.parse(text, field.locale)));
            if (v !== field.value)
                field.edited(v / field.factor);
            text = Qt.binding(() => field.displayText); // "10" reads "10.00" again
        }

        TextMetrics {
            id: widest
            font: field.font
            text: field.format(field.to, field.locale)
        }
    }
}
