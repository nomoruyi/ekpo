import QtQuick
import QtTest
import "../../src/app/qml"

// Typing into a decimal field (issue #1: selecting the text and typing 10 gave 1.000).
TestCase {
    id: test

    property var edits: []

    name: "DecimalField"
    when: windowShown
    visible: true
    width: 300
    height: 80

    TextInput {
        id: other
        y: 50
        width: 100
        text: "elsewhere"
    }
    DecimalField {
        id: field
        number: 25
        onEdited: v => {
            test.edits = test.edits.concat([v]);
            number = v;
        }
    }

    function init() {
        field.number = 25;
        test.edits = [];
    }

    function test_typesTwoDigits() {
        field.contentItem.forceActiveFocus();
        field.contentItem.selectAll();
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_0);
        compare(field.contentItem.text, "10"); // not rewritten while typing
        keyClick(Qt.Key_Return);
        compare(test.edits, [10]);
        compare(field.contentItem.text, Number(10).toLocaleString(field.locale, "f", 2));
    }

    function test_typesDecimals() {
        field.contentItem.forceActiveFocus();
        field.contentItem.selectAll();
        for (const key of ["0", field.locale.decimalPoint, "0", "5"])
            keyClick(key);
        other.forceActiveFocus(); // leaving the field takes the number too
        compare(test.edits, [0.05]);
    }

    function test_followsNumber() {
        field.contentItem.forceActiveFocus();
        field.contentItem.selectAll();
        keyClick(Qt.Key_7);
        keyClick(Qt.Key_Return);
        field.number = 42; // another element selected
        compare(field.value, 4200);
    }
}
