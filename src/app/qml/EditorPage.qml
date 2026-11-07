pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Ekpo

// The design editor (SPEC.md §4.4, notes/wireframes-round8.md §3): elements on the
// left, the canvas with the generated Splash.qml as its preview, properties on the
// right. Plasma splash designs only so far.
Kirigami.Page {
    id: page

    signal leave
    signal review

    readonly property DesignEditor editor: Backend.editor
    readonly property var elements: editor.content.elements ?? []
    readonly property var backgroundSpec: editor.content.background ?? ({})
    property int selected: -1 // -1 = the background
    readonly property var element: selected >= 0 && selected < elements.length ? elements[selected] : null
    property int resolution: 1440
    readonly property size screenSize: Qt.size(resolution * 16 / 9, resolution)
    readonly property real tolerance: Kirigami.Units.smallSpacing * 1.5 // snapping, in canvas pixels
    property real guideX: -1
    property real guideY: -1
    property string message: ""
    readonly property bool boot: editor.stage === "bootSplash"
    readonly property bool grub: editor.stage === "bootMenu"
    readonly property bool plasma: editor.stage === "plasmaSplash"
    readonly property bool hasMenu: elements.some(e => e.type === "menu") // a boot menu has one
    readonly property string screenKey: "%1x%2".arg(screenSize.width).arg(screenSize.height)
    onScreenSizeChanged: editor.setPreviewScreen(screenSize.width, screenSize.height)
    Component.onCompleted: editor.setPreviewScreen(screenSize.width, screenSize.height)
    // A boot splash has one animation and one progress bar (genplymouth.h).
    readonly property bool hasAnimation: elements.some(e => e.type === "spinner" || e.type === "frames")
    readonly property bool hasProgress: elements.some(e => e.type === "progress")
    property string pendingAdd: "" // what a running conversion will add: "video" or "frames"
    // Shared elements (SPEC.md §3): the design's palette and the library's logos.
    readonly property string paletteId: editor.content.palette ?? ""
    readonly property var sharedLogos: Object.keys(editor.shared).filter(id => editor.shared[id].kind === "logo")
    readonly property var palettes: Object.keys(editor.shared).filter(id => editor.shared[id].kind === "palette")
    readonly property var grubImports: Backend.imports.filter(t => t.stage === "bootMenu") // icons can come from them

    // The palette's colours (SPEC.md §4.4), as "@key" in a design.
    readonly property var paletteColours: [{ key: "background", name: qsTr("Background") }, { key: "gradient", name: qsTr("Gradient") },
        { key: "accent", name: qsTr("Accent") }, { key: "accent2", name: qsTr("Second accent") }, { key: "text", name: qsTr("Text") }]

    // A colour as shown: "@accent" and so on come from the palette.
    function colour(raw) {
        if (typeof raw === "string" && raw.startsWith("@"))
            return editor.shared[paletteId]?.colours?.[raw.slice(1)] ?? "#000000";
        return raw;
    }
    // Shown imperatively: the close button sets visible = false, which would remove a binding.
    onMessageChanged: messageBox.visible = message !== ""

    function label(e) {
        if (e.type === "text")
            return e.text.split("\n")[0] || qsTr("(empty text)");
        if (e.shared)
            return editor.shared[e.shared]?.name ?? qsTr("(missing shared logo)");
        if (e.type === "image" || e.type === "video")
            return editor.assets[e.asset]?.name ?? (e.type === "image" ? qsTr("(missing image)") : qsTr("(missing video)"));
        return ({ spinner: qsTr("Spinner"), frames: qsTr("Animation"), progress: qsTr("Progress bar"), menu: qsTr("Menu"), countdown: qsTr("Countdown") })[e.type] ?? e.type;
    }
    function iconFor(e) {
        return ({ text: "draw-text", image: "viewimage", spinner: "view-refresh", video: "video-x-generic", frames: "media-playlist-repeat",
                  progress: "view-task", menu: "view-list-details", countdown: "chronometer" })[e.type] ?? "unknown";
    }
    // Fields commit when they lose focus, so take it before acting on the design.
    function commitFields() {
        canvas.forceActiveFocus();
    }
    function saveDesign(): bool {
        commitFields();
        message = editor.save();
        return message === "";
    }
    function tryLeave() {
        commitFields();
        if (editor.dirty) {
            leaveDialog.open();
        } else {
            editor.close();
            leave();
        }
    }
    function nudge(dx, dy) {
        if (element)
            editor.setElement(selected, { x: element.x + dx, y: element.y + dy });
    }
    function megabytes(bytes) {
        return (bytes / 1048576).toFixed(1);
    }
    function endDrag() {
        editor.endGesture();
        guideX = guideY = -1;
    }

    title: editor.name
    padding: 0
    onElementsChanged: if (selected >= elements.length) selected = elements.length - 1

    titleDelegate: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.ToolButton {
            icon.name: "go-previous"
            text: qsTr("Back")
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: page.tryLeave()
        }
        QQC2.TextField {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            text: page.editor.name
            placeholderText: qsTr("Name")
            onEditingFinished: page.editor.name = text
        }
        QQC2.Label {
            opacity: 0.7
            text: page.editor.version === 0 ? qsTr("not saved yet")
                : page.editor.dirty ? qsTr("version %1, changed").arg(page.editor.version) : qsTr("version %1").arg(page.editor.version)
        }
    }

    // (Run Real Splash only for Plasma splashes: a list without it, not a hidden action: Kirigami
    // 6.24 can't lay out hidden actions.)
    actions: plasma ? [undoAction, redoAction, saveAction, versionsAction, runAction, applyAction, bugAction]
                    : [undoAction, redoAction, saveAction, versionsAction, applyAction, bugAction]

    ReportBugAction {
        id: bugAction
    }

    Kirigami.Action {
        id: undoAction
        text: qsTr("Undo")
        icon.name: "edit-undo"
        shortcut: StandardKey.Undo
        displayHint: Kirigami.DisplayHint.IconOnly
        enabled: page.editor.canUndo && page.isCurrentPage // the editor stays open behind the review
        onTriggered: page.editor.undo()
    }
    Kirigami.Action {
        id: redoAction
        text: qsTr("Redo")
        icon.name: "edit-redo"
        shortcut: StandardKey.Redo
        displayHint: Kirigami.DisplayHint.IconOnly
        enabled: page.editor.canRedo && page.isCurrentPage
        onTriggered: page.editor.redo()
    }
    Kirigami.Action {
        id: saveAction
        text: qsTr("Save")
        icon.name: "document-save"
        shortcut: StandardKey.Save
        enabled: page.editor.dirty && page.isCurrentPage
        onTriggered: page.saveDesign()
    }
    Kirigami.Action {
        id: versionsAction
        text: qsTr("Versions…")
        icon.name: "view-history"
        enabled: page.editor.versions.length > 0
        onTriggered: versionsDialog.open()
    }
    Kirigami.Action {
        id: runAction
        text: qsTr("Run Real Splash")
        icon.name: "media-playback-start"
        onTriggered: page.message = page.editor.runRealSplash()
    }
    Kirigami.Action {
        id: applyAction
        text: qsTr("Apply…")
        icon.name: "dialog-ok-apply"
        enabled: page.editor.problems.length === 0 && !Backend.applying
        onTriggered: {
            if (page.editor.dirty && !page.saveDesign())
                return;
            Backend.planApplyDesign(page.editor.designId);
            page.review();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.editor.recovered
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Information
            text: qsTr("Unsaved changes from an earlier session were recovered.")
            actions: Kirigami.Action {
                text: qsTr("Discard Them")
                icon.name: "edit-delete"
                onTriggered: {
                    page.editor.discard();
                    if (!page.editor.isOpen)
                        page.leave();
                }
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.editor.conversionProgress >= 0
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Information
            text: qsTr("Converting “%1” for the splash: %2 %").arg(page.editor.conversionName).arg(Math.round(page.editor.conversionProgress * 100))
            actions: Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: page.editor.cancelConversion()
            }
        }
        Kirigami.InlineMessage {
            id: messageBox
            Layout.fillWidth: true
            visible: false
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Error
            showCloseButton: true
            text: page.message
            onVisibleChanged: if (!visible) page.message = ""
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Elements, top to bottom in drawing order.
            ColumnLayout {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 0

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        // A count, not the array: delegates stay while an element is dragged.
                        model: page.elements.length + 1
                        currentIndex: page.selected + 1
                        delegate: QQC2.ItemDelegate {
                            required property int index
                            readonly property var spec: index > 0 ? page.elements[index - 1] : null

                            width: ListView.view.width
                            text: spec ? page.label(spec) : qsTr("Background")
                            icon.name: spec ? page.iconFor(spec) : "fill-color"
                            highlighted: ListView.isCurrentItem
                            onClicked: {
                                page.selected = index - 1;
                                canvas.forceActiveFocus(); // arrow keys nudge the selection
                            }
                        }
                    }
                }
                Kirigami.Separator {
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: 0

                    QQC2.ToolButton {
                        id: addButton
                        icon.name: "list-add"
                        text: qsTr("Add")
                        onClicked: (page.grub ? grubAddMenu : page.boot ? bootAddMenu : addMenu).popup(addButton, 0, addButton.height)

                        QQC2.Menu {
                            id: addMenu

                            QQC2.MenuItem {
                                text: qsTr("Shared Logo…")
                                icon.name: "emblem-shared-symbolic"
                                enabled: page.sharedLogos.length > 0
                                onTriggered: logoDialog.open()
                            }
                            QQC2.MenuItem {
                                text: qsTr("Text")
                                icon.name: "draw-text"
                                onTriggered: page.selected = page.editor.addElement("text")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Image…")
                                icon.name: "viewimage"
                                onTriggered: {
                                    imageDialog.purpose = "add";
                                    imageDialog.open();
                                }
                            }
                            QQC2.MenuItem {
                                text: qsTr("Video…")
                                icon.name: "video-x-generic"
                                enabled: page.editor.conversionProgress < 0
                                onTriggered: {
                                    videoDialog.purpose = "add";
                                    videoDialog.open();
                                }
                            }
                            QQC2.MenuItem {
                                text: qsTr("Spinner")
                                icon.name: "view-refresh"
                                onTriggered: page.selected = page.editor.addElement("spinner")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Animation from Images…")
                                icon.name: "media-playlist-repeat"
                                onTriggered: {
                                    framesImageDialog.replaceIndex = -1;
                                    framesImageDialog.open();
                                }
                            }
                            QQC2.MenuItem {
                                text: qsTr("Progress Bar")
                                icon.name: "view-task"
                                onTriggered: page.selected = page.editor.addElement("progress")
                            }
                        }
                        QQC2.Menu {
                            id: grubAddMenu

                            QQC2.MenuItem {
                                text: qsTr("Menu")
                                icon.name: "view-list-details"
                                enabled: !page.hasMenu
                                onTriggered: page.selected = page.editor.addElement("menu")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Countdown")
                                icon.name: "chronometer"
                                onTriggered: page.selected = page.editor.addElement("countdown")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Progress Bar")
                                icon.name: "view-task"
                                onTriggered: page.selected = page.editor.addElement("progress")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Text")
                                icon.name: "draw-text"
                                onTriggered: page.selected = page.editor.addElement("text")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Image…")
                                icon.name: "viewimage"
                                onTriggered: {
                                    imageDialog.purpose = "add";
                                    imageDialog.open();
                                }
                            }
                            QQC2.MenuItem {
                                text: qsTr("Shared Logo…")
                                icon.name: "emblem-shared-symbolic"
                                enabled: page.sharedLogos.length > 0
                                onTriggered: logoDialog.open()
                            }
                        }
                        QQC2.Menu {
                            id: bootAddMenu

                            QQC2.MenuItem {
                                text: qsTr("Shared Logo…")
                                icon.name: "emblem-shared-symbolic"
                                enabled: page.sharedLogos.length > 0
                                onTriggered: logoDialog.open()
                            }
                            QQC2.MenuItem {
                                text: qsTr("Text")
                                icon.name: "draw-text"
                                onTriggered: page.selected = page.editor.addElement("text")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Image…")
                                icon.name: "viewimage"
                                onTriggered: {
                                    imageDialog.purpose = "add";
                                    imageDialog.open();
                                }
                            }
                            QQC2.MenuSeparator {}
                            QQC2.MenuItem {
                                text: qsTr("Spinner")
                                icon.name: "view-refresh"
                                enabled: !page.hasAnimation
                                onTriggered: page.selected = page.editor.addElement("spinner")
                            }
                            QQC2.MenuItem {
                                text: qsTr("Animation from Images…")
                                icon.name: "media-playlist-repeat"
                                enabled: !page.hasAnimation
                                onTriggered: {
                                    framesImageDialog.replaceIndex = -1;
                                    framesImageDialog.open();
                                }
                            }
                            QQC2.MenuItem {
                                text: qsTr("Animation from Video…")
                                icon.name: "video-x-generic"
                                enabled: !page.hasAnimation && page.editor.conversionProgress < 0
                                onTriggered: {
                                    framesVideoDialog.replaceIndex = -1;
                                    framesVideoDialog.open();
                                }
                            }
                            QQC2.MenuSeparator {}
                            QQC2.MenuItem {
                                text: qsTr("Progress Bar")
                                icon.name: "view-task"
                                enabled: !page.hasProgress
                                onTriggered: page.selected = page.editor.addElement("progress")
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    IconButton {
                        icon.name: "go-up"
                        text: qsTr("Draw Earlier")
                        enabled: page.selected > 0
                        onClicked: {
                            page.editor.moveElement(page.selected, page.selected - 1);
                            page.selected -= 1;
                        }
                    }
                    IconButton {
                        icon.name: "go-down"
                        text: qsTr("Draw Later")
                        enabled: page.selected >= 0 && page.selected < page.elements.length - 1
                        onClicked: {
                            page.editor.moveElement(page.selected, page.selected + 1);
                            page.selected += 1;
                        }
                    }
                    IconButton {
                        icon.name: "edit-delete"
                        text: qsTr("Remove")
                        enabled: page.selected >= 0
                        onClicked: page.editor.removeElement(page.selected)
                    }
                }
            }
            Kirigami.Separator {
                Layout.fillHeight: true
            }

            // The canvas: the real Splash.qml at the chosen resolution, scaled to fit,
            // with a box per element to select, drag and resize.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.margins: Kirigami.Units.smallSpacing

                    Repeater {
                        model: [1440, 1080, 720]
                        delegate: QQC2.ToolButton {
                            required property int modelData
                            text: qsTr("%1p").arg(modelData)
                            checkable: true
                            autoExclusive: true
                            checked: page.resolution === modelData
                            onClicked: page.resolution = modelData
                        }
                    }
                }
                Item {
                    id: canvas

                    readonly property real zoom: Math.max(0.01, Math.min((width - Kirigami.Units.gridUnit * 2) / page.screenSize.width,
                                                                          (height - Kirigami.Units.gridUnit * 2) / page.screenSize.height))

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    activeFocusOnTab: true

                    Keys.onPressed: event => {
                        const step = event.modifiers & Qt.ShiftModifier ? 1 : 0.1;
                        if (event.key === Qt.Key_Left)
                            page.nudge(-step, 0);
                        else if (event.key === Qt.Key_Right)
                            page.nudge(step, 0);
                        else if (event.key === Qt.Key_Up)
                            page.nudge(0, -step);
                        else if (event.key === Qt.Key_Down)
                            page.nudge(0, step);
                        else if (event.key === Qt.Key_Delete && page.selected >= 0)
                            page.editor.removeElement(page.selected);
                        else
                            return;
                        event.accepted = true;
                    }

                    Rectangle {
                        id: monitor

                        anchors.centerIn: parent
                        width: page.screenSize.width * canvas.zoom
                        height: page.screenSize.height * canvas.zoom
                        color: "black"
                        clip: true

                        Loader {
                            id: preview
                            width: page.screenSize.width
                            height: page.screenSize.height
                            scale: canvas.zoom
                            transformOrigin: Item.TopLeft
                            source: page.plasma ? page.editor.previewUrl : ""
                            // Midway through Plasma's start, so progress bars show (ksplashqml sets 1–6).
                            onLoaded: item.stage = 3
                        }
                        // A boot menu: drawn by Ekpo as GRUB will draw it, in GRUB's fonts.
                        Image {
                            anchors.fill: parent
                            visible: page.grub
                            source: page.grub ? (page.editor.bootMenu.preview ?? "") : ""
                            smooth: true
                            mipmap: true
                        }
                        PlymouthPreview {
                            visible: page.boot
                            layout: page.editor.plymouth
                            width: page.screenSize.width
                            height: page.screenSize.height
                            scale: canvas.zoom
                            transformOrigin: Item.TopLeft
                        }
                        MouseArea {
                            anchors.fill: parent
                            onPressed: {
                                canvas.forceActiveFocus();
                                page.selected = -1;
                            }
                        }
                        Repeater {
                            model: page.elements.length // a count, see the element list
                            delegate: ElementBox {}
                        }
                        Rectangle {
                            visible: page.guideX >= 0
                            x: page.guideX / 100 * monitor.width
                            width: 1
                            height: monitor.height
                            color: Kirigami.Theme.highlightColor
                        }
                        Rectangle {
                            visible: page.guideY >= 0
                            y: page.guideY / 100 * monitor.height
                            width: monitor.width
                            height: 1
                            color: Kirigami.Theme.highlightColor
                        }
                    }
                    QQC2.Label {
                        anchors.centerIn: parent
                        visible: page.plasma && preview.status === Loader.Error
                        text: qsTr("The preview couldn't be loaded.")
                    }
                }
                // A boot splash is copied into every initrd and fully decoded before it
                // shows (SPEC.md §4.6).
                RowLayout {
                    visible: page.boot
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: qsTr("Size")
                    }
                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        to: page.editor.plymouth.capBytes ?? 1
                        value: Math.min(to, page.editor.plymouth.bytes ?? 0)
                    }
                    QQC2.Label {
                        text: qsTr("%1 of %2 MB").arg(page.megabytes(page.editor.plymouth.bytes ?? 0)).arg(page.megabytes(page.editor.plymouth.capBytes ?? 0))
                    }
                    QQC2.Label {
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        text: qsTr("Memory")
                    }
                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        to: page.editor.plymouth.budgetBytes ?? 1
                        value: Math.min(to, page.editor.plymouth.memory ?? 0)
                    }
                    QQC2.Label {
                        text: qsTr("%1 of %2 MB").arg(page.megabytes(page.editor.plymouth.memory ?? 0)).arg(page.megabytes(page.editor.plymouth.budgetBytes ?? 0))
                    }
                }
            }
            Kirigami.Separator {
                Layout.fillHeight: true
            }

            QQC2.ScrollView {
                id: propertiesView
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                Layout.fillHeight: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                Kirigami.FormLayout {
                    width: propertiesView.availableWidth

                    // Background and package
                    RowLayout {
                        visible: page.selected < 0
                        Kirigami.FormData.label: qsTr("Palette:")
                        QQC2.ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("None")].concat(page.palettes.map(id => page.editor.shared[id].name)).concat([qsTr("New from This Design")])
                            currentIndex: page.paletteId === "" ? 0 : page.palettes.indexOf(page.paletteId) + 1
                            onActivated: index => {
                                if (index === 0)
                                    page.editor.linkPalette("");
                                else if (index === count - 1)
                                    page.message = page.editor.newPalette("");
                                else
                                    page.editor.linkPalette(page.palettes[index - 1]);
                            }
                        }
                        IconButton {
                            text: qsTr("Delete Palette…")
                            icon.name: "edit-delete"
                            enabled: page.paletteId !== ""
                            onClicked: deletePaletteDialog.open()
                        }
                    }
                    RowLayout {
                        visible: page.selected < 0 && page.paletteId !== ""
                        Kirigami.FormData.label: qsTr("Palette colours:")
                        Repeater {
                            model: page.paletteColours
                            delegate: ColorButton {
                                required property var modelData
                                implicitWidth: Kirigami.Units.gridUnit * 1.6
                                value: page.editor.shared[page.paletteId]?.colours?.[modelData.key] ?? "#000000"
                                QQC2.ToolTip.text: modelData.name
                                QQC2.ToolTip.visible: hovered
                                onPicked: c => page.message = page.editor.setPaletteColour(modelData.key, c)
                            }
                        }
                    }
                    QQC2.Label {
                        visible: page.selected < 0 && page.paletteId !== ""
                        Layout.fillWidth: true
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 13 // a wrapping label would widen the form
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("Background, gradient, accent, second accent and text. Changing them changes every design that uses this palette (%n), once it is applied again.", "",
                                   page.editor.shared[page.paletteId]?.uses ?? 0)
                    }
                    ColourField {
                        visible: page.selected < 0
                        Kirigami.FormData.label: qsTr("Colour:")
                        raw: page.backgroundSpec.color ?? "#000000"
                        onPicked: value => page.editor.setBackground({ color: value })
                    }
                    AssetRow {
                        visible: page.selected < 0
                        Kirigami.FormData.label: qsTr("Image:")
                        asset: page.backgroundSpec.image ?? ""
                        onChoose: {
                            imageDialog.purpose = "background";
                            imageDialog.open();
                        }
                        onRemove: page.editor.setBackground({ image: null })
                    }
                    RowLayout {
                        visible: page.selected < 0
                        Kirigami.FormData.label: qsTr("Gradient to:")
                        QQC2.CheckBox {
                            checked: page.backgroundSpec.color2 !== undefined
                            onToggled: page.editor.setBackground({ color2: checked ? (page.backgroundSpec.color ?? "#000000") : null })
                        }
                        ColourField {
                            enabled: page.backgroundSpec.color2 !== undefined
                            raw: page.backgroundSpec.color2 ?? page.backgroundSpec.color ?? "#000000"
                            onPicked: value => page.editor.setBackground({ color2: value })
                        }
                    }
                    // As in CSS: 180° from the first colour at the top to the second at the bottom.
                    DecimalField {
                        visible: page.selected < 0 && page.backgroundSpec.color2 !== undefined
                        Kirigami.FormData.label: qsTr("Gradient angle (°):")
                        QQC2.ToolTip.text: qsTr("180° runs from the first colour at the top to the second at the bottom, 90° from left to right.")
                        QQC2.ToolTip.visible: hovered
                        decimals: 0
                        from: 0
                        to: 359
                        stepSize: 15
                        wrap: true
                        number: page.backgroundSpec.angle ?? 180
                        onEdited: v => page.editor.setBackground({ angle: v })
                    }
                    QQC2.CheckBox {
                        visible: page.selected < 0 && page.boot
                        Kirigami.FormData.label: qsTr("Status messages:")
                        text: qsTr("Hide")
                        checked: page.editor.content.hideMessages ?? false
                        onToggled: page.editor.setContent({ hideMessages: checked })
                    }
                    // One form row: FormLayout can't follow a Repeater's rows coming and going.
                    ColumnLayout {
                        visible: page.selected < 0 && page.grub
                        Layout.fillWidth: true
                        Kirigami.FormData.label: qsTr("Entry icons:")
                        RowLayout {
                            QQC2.Button {
                                text: qsTr("Built-in")
                                icon.name: "view-list-icons"
                                onClicked: page.editor.useBuiltinIcons()
                            }
                            QQC2.Button {
                                id: importIconsButton
                                text: qsTr("From a Theme…")
                                icon.name: "document-import"
                                enabled: page.grubImports.length > 0
                                onClicked: importIconsMenu.popup(importIconsButton, 0, importIconsButton.height)

                                QQC2.Menu {
                                    id: importIconsMenu
                                    Instantiator {
                                        model: page.grubImports
                                        delegate: QQC2.MenuItem {
                                            required property var modelData
                                            text: modelData.name
                                            onTriggered: page.message = page.editor.useImportIcons(modelData.id)
                                        }
                                        onObjectAdded: (index, object) => importIconsMenu.insertItem(index, object)
                                        onObjectRemoved: (index, object) => importIconsMenu.removeItem(object)
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: page.editor.iconClasses
                            delegate: RowLayout {
                                id: iconRow

                                required property string modelData
                                readonly property var icons: page.editor.content.icons ?? ({})

                                Layout.fillWidth: true
                                QQC2.Label {
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                                    text: iconRow.modelData
                                    opacity: 0.7
                                }
                                AssetRow {
                                    Layout.fillWidth: true
                                    asset: iconRow.icons[iconRow.modelData] ?? ""
                                    onChoose: {
                                        imageDialog.purpose = "icon:" + iconRow.modelData;
                                        imageDialog.open();
                                    }
                                    onRemove: {
                                        const all = Object.assign({}, iconRow.icons);
                                        delete all[iconRow.modelData];
                                        page.editor.setContent({ icons: Object.keys(all).length > 0 ? all : null });
                                    }
                                }
                            }
                        }
                    }
                    QQC2.SpinBox {
                        visible: page.selected < 0 && page.plasma
                        Kirigami.FormData.label: qsTr("Fade-in (ms):")
                        from: 0
                        to: 5000
                        stepSize: 100
                        editable: true
                        value: page.editor.content.fadeIn ?? 0
                        onValueModified: page.editor.setContent({ fadeIn: value })
                    }
                    Kirigami.Separator {
                        visible: page.selected < 0 && page.plasma
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: qsTr("Package")
                    }
                    QQC2.TextField {
                        visible: page.selected < 0 && page.plasma
                        Kirigami.FormData.label: qsTr("Author:")
                        text: page.editor.content.author ?? "nomoruyi"
                        onEditingFinished: page.editor.setContent({ author: text })
                    }
                    QQC2.TextField {
                        visible: page.selected < 0 && page.plasma
                        Kirigami.FormData.label: qsTr("Licence:")
                        text: page.editor.content.licence ?? "MIT"
                        onEditingFinished: page.editor.setContent({ licence: text })
                    }

                    // Text
                    QQC2.TextArea {
                        visible: ["text", "countdown"].includes(page.element?.type)
                        Layout.fillWidth: true
                        Kirigami.FormData.label: qsTr("Text:")
                        text: page.element?.text ?? ""
                        wrapMode: TextEdit.Wrap
                        onEditingFinished: page.editor.setElement(page.selected, { text: text })
                    }
                    QQC2.Label {
                        visible: page.element?.type === "countdown"
                        Layout.fillWidth: true
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 13
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: qsTr("GRUB puts the seconds left where the text says %d.")
                    }
                    QQC2.ComboBox {
                        visible: ["text", "menu", "countdown"].includes(page.element?.type)
                        Layout.fillWidth: true
                        Kirigami.FormData.label: qsTr("Font:")
                        model: [qsTr("Default font")].concat(Qt.fontFamilies())
                        displayText: page.element?.font ? (page.editor.assets[page.element.font]?.name ?? qsTr("(missing font)")) : qsTr("Default font")
                        onActivated: index => {
                            if (index === 0)
                                return page.editor.setElement(page.selected, { font: null });
                            const added = page.editor.addSystemFont(currentText);
                            if (added.error)
                                page.message = added.error;
                            else
                                page.editor.setElement(page.selected, { font: added.hash });
                        }
                    }
                    RowLayout {
                        visible: ["text", "menu", "countdown"].includes(page.element?.type)
                        Kirigami.FormData.label: qsTr("Size (% of height):")
                        DecimalField {
                            number: page.element?.size ?? 3
                            onEdited: v => page.editor.setElement(page.selected, { size: v })
                        }
                        QQC2.Label {
                            opacity: 0.7
                            text: qsTr("%1 px").arg(Math.floor(page.resolution * (page.element?.size ?? 3) / 100))
                        }
                    }
                    QQC2.ComboBox {
                        visible: ["text", "countdown"].includes(page.element?.type)
                        Kirigami.FormData.label: qsTr("Alignment:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{ value: "left", text: qsTr("Left") }, { value: "center", text: qsTr("Centre") }, { value: "right", text: qsTr("Right") }]
                        currentIndex: Math.max(0, ["left", "center", "right"].indexOf(page.element?.align ?? "center"))
                        onActivated: page.editor.setElement(page.selected, { align: currentValue })
                    }
                    ColourField {
                        visible: ["text", "spinner", "progress", "menu", "countdown"].includes(page.element?.type)
                        Kirigami.FormData.label: qsTr("Colour:")
                        raw: page.element?.color ?? "#ffffff"
                        onPicked: value => page.editor.setElement(page.selected, { color: value })
                    }

                    // Boot menu: the selected entry, and the font size at this resolution
                    ColourField {
                        visible: page.element?.type === "menu"
                        Kirigami.FormData.label: qsTr("Selected text:")
                        raw: page.element?.selectedColor ?? "#ffffff"
                        onPicked: value => page.editor.setElement(page.selected, { selectedColor: value })
                    }
                    RowLayout {
                        visible: page.element?.type === "menu"
                        Kirigami.FormData.label: qsTr("Selection:")
                        QQC2.ComboBox {
                            textRole: "text"
                            valueRole: "value"
                            model: [{ value: "flat", text: qsTr("Flat") }, { value: "rounded", text: qsTr("Rounded") }, { value: "glow", text: qsTr("Glow") }]
                            currentIndex: Math.max(0, ["flat", "rounded", "glow"].indexOf(page.element?.selection?.style ?? "rounded"))
                            onActivated: page.editor.setElement(page.selected, { selection: { style: currentValue, color: page.element?.selection?.color ?? "#40ffffff" } })
                        }
                        ColourField {
                            raw: page.element?.selection?.color ?? "#40ffffff"
                            onPicked: value => page.editor.setElement(page.selected, { selection: { style: page.element?.selection?.style ?? "rounded", color: value } })
                        }
                    }
                    RowLayout {
                        id: overrideRow

                        readonly property var overrides: page.editor.content.sizeOverrides ?? ({})
                        function setOverride(pixels) {
                            const all = Object.assign({}, overrides);
                            if (pixels > 0)
                                all[page.screenKey] = pixels;
                            else
                                delete all[page.screenKey];
                            page.editor.setContent({ sizeOverrides: Object.keys(all).length > 0 ? all : null });
                        }

                        visible: page.element?.type === "menu"
                        Kirigami.FormData.label: qsTr("Font at %1p:").arg(page.resolution)
                        QQC2.CheckBox {
                            text: qsTr("Own size")
                            checked: overrideRow.overrides[page.screenKey] !== undefined
                            onToggled: overrideRow.setOverride(checked ? Math.floor(page.resolution * (page.element?.size ?? 3) / 100) : 0)
                        }
                        QQC2.SpinBox {
                            enabled: overrideRow.overrides[page.screenKey] !== undefined
                            from: 8
                            to: 200
                            value: overrideRow.overrides[page.screenKey] ?? Math.floor(page.resolution * (page.element?.size ?? 3) / 100)
                            onValueModified: overrideRow.setOverride(value)
                        }
                        QQC2.Label {
                            text: qsTr("px")
                        }
                    }

                    // Image
                    AssetRow {
                        visible: page.element?.type === "image"
                        Kirigami.FormData.label: qsTr("Image:")
                        asset: page.element?.shared ? (page.editor.shared[page.element.shared]?.asset ?? "") : (page.element?.asset ?? "")
                        removable: false
                        onChoose: {
                            // A shared logo changes for every design that links it.
                            imageDialog.purpose = page.element?.shared ? "shared" : "replace";
                            imageDialog.open();
                        }
                    }
                    RowLayout {
                        id: logoRow

                        readonly property var logo: page.element?.shared ? (page.editor.shared[page.element.shared] ?? null) : null

                        visible: page.element?.type === "image"
                        Kirigami.FormData.label: qsTr("Shared logo:")
                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: logoRow.logo ? qsTr("%1, in %n design(s)", "", logoRow.logo.uses).arg(logoRow.logo.name) : qsTr("No")
                        }
                        QQC2.Button {
                            visible: !logoRow.logo
                            text: qsTr("Share…")
                            onClicked: shareDialog.open()
                        }
                        QQC2.Button {
                            visible: logoRow.logo !== null
                            text: qsTr("Unlink")
                            onClicked: page.editor.unlink(page.selected)
                        }
                    }

                    // Video
                    AssetRow {
                        visible: page.element?.type === "video"
                        Kirigami.FormData.label: qsTr("Video:")
                        asset: page.element?.asset ?? ""
                        removable: false
                        onChoose: {
                            videoDialog.purpose = "replace";
                            videoDialog.open();
                        }
                    }
                    QQC2.ComboBox {
                        visible: page.element?.type === "video"
                        Kirigami.FormData.label: qsTr("Fill:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{ value: "crop", text: qsTr("Cover the box (crop)") }, { value: "fit", text: qsTr("Fit inside the box") }]
                        currentIndex: page.element?.fill === "fit" ? 1 : 0
                        onActivated: page.editor.setElement(page.selected, { fill: currentValue })
                    }

                    // Boot splash: progress bar and animation
                    ColourField {
                        visible: page.element?.type === "progress"
                        Kirigami.FormData.label: qsTr("Background:")
                        raw: page.element?.background ?? "#404040"
                        onPicked: value => page.editor.setElement(page.selected, { background: value })
                    }
                    QQC2.Label {
                        visible: page.element?.type === "frames"
                        Kirigami.FormData.label: qsTr("Frames:")
                        text: page.element?.frames?.length ?? 0
                    }
                    DecimalField {
                        visible: page.element?.type === "frames"
                        Kirigami.FormData.label: qsTr("Loop (s):")
                        from: 10
                        number: page.element?.seconds ?? 2
                        onEdited: v => page.editor.setElement(page.selected, { seconds: v })
                    }
                    QQC2.Label {
                        visible: page.element?.type === "frames" && page.boot
                        Layout.fillWidth: true
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 13
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: Math.abs((page.element?.seconds ?? 2) - 2) < 0.01 ? qsTr("Plays with two-step, which loops every animation in 2 s.")
                                                                                  : qsTr("Plays with Plymouth's script module (two-step only loops in 2 s).")
                    }
                    RowLayout {
                        visible: page.element?.type === "frames"
                        Kirigami.FormData.label: qsTr("Replace:")
                        QQC2.Button {
                            text: qsTr("Images…")
                            onClicked: {
                                framesImageDialog.replaceIndex = page.selected;
                                framesImageDialog.open();
                            }
                        }
                        QQC2.Button {
                            visible: page.boot // a Plasma splash plays the video itself
                            text: qsTr("Video…")
                            enabled: page.editor.conversionProgress < 0
                            onClicked: {
                                framesVideoDialog.replaceIndex = page.selected;
                                framesVideoDialog.open();
                            }
                        }
                    }

                    // Position and size of any element
                    Kirigami.Separator {
                        visible: page.element !== null
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: qsTr("Position (% of the screen)")
                    }
                    DecimalField {
                        visible: page.element !== null
                        Kirigami.FormData.label: qsTr("X:")
                        number: page.element?.x ?? 0
                        onEdited: v => page.editor.setElement(page.selected, { x: v })
                    }
                    DecimalField {
                        visible: page.element !== null
                        Kirigami.FormData.label: qsTr("Y:")
                        number: page.element?.y ?? 0
                        onEdited: v => page.editor.setElement(page.selected, { y: v })
                    }
                    DecimalField {
                        visible: page.element !== null
                        Kirigami.FormData.label: qsTr("Width:")
                        number: page.element?.width ?? 0
                        onEdited: v => page.editor.setElement(page.selected, { width: v })
                    }
                    DecimalField {
                        visible: page.element !== null
                        Kirigami.FormData.label: qsTr("Height:")
                        number: page.element?.height ?? 0
                        onEdited: v => page.editor.setElement(page.selected, { height: v })
                    }
                }
            }
        }

        // The warnings bar: problems block applying, warnings don't.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.editor.problems.length > 0
            position: Kirigami.InlineMessage.Position.Footer
            type: Kirigami.MessageType.Error
            text: page.editor.problems.join("\n")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.editor.warnings.length > 0
            position: Kirigami.InlineMessage.Position.Footer
            type: Kirigami.MessageType.Warning
            text: page.editor.warnings.join("\n")
        }
    }

    component IconButton: QQC2.ToolButton {
        display: QQC2.AbstractButton.IconOnly
        QQC2.ToolTip.text: text
        QQC2.ToolTip.visible: hovered
    }

    component ColorButton: QQC2.Button {
        id: colorButton

        property color value
        signal picked(color value)

        implicitWidth: Kirigami.Units.gridUnit * 4
        contentItem: Rectangle {
            color: colorButton.value
            border.color: Kirigami.Theme.textColor
            border.width: 1
            radius: Kirigami.Units.cornerRadius
        }
        onClicked: {
            colorDialog.target = colorButton;
            colorPicker.load(value);
            colorDialog.open();
        }
    }

    // A colour of its own, or one of the palette's ("@accent" and so on).
    component ColourField: RowLayout {
        id: colourField

        property string raw
        signal picked(var value) // a colour, or a palette colour such as "@accent"

        ColorButton {
            value: page.colour(colourField.raw)
            onPicked: c => colourField.picked(c)
        }
        QQC2.ToolButton {
            visible: page.paletteId !== ""
            icon.name: "color-management"
            text: qsTr("Use a Palette Colour")
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: paletteMenu.popup()

            QQC2.Menu {
                id: paletteMenu
                Repeater {
                    model: page.paletteColours
                    delegate: QQC2.MenuItem {
                        required property var modelData
                        text: qsTr("Palette %1").arg(modelData.name)
                        onTriggered: colourField.picked("@" + modelData.key)
                    }
                }
            }
        }
        QQC2.Label {
            visible: colourField.raw.startsWith("@")
            opacity: 0.7
            text: qsTr("palette")
        }
    }

    component AssetRow: RowLayout {
        id: assetRow

        property string asset
        property bool removable: true
        signal choose
        signal remove

        QQC2.Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: assetRow.asset === "" ? qsTr("None") : assetRow.asset.startsWith("builtin:") ? qsTr("Built-in")
                : (page.editor.assets[assetRow.asset]?.name ?? qsTr("(missing)"))
        }
        IconButton {
            icon.name: "document-open"
            text: qsTr("Choose…")
            onClicked: assetRow.choose()
        }
        IconButton {
            visible: assetRow.removable && assetRow.asset !== ""
            icon.name: "edit-clear"
            text: qsTr("Remove")
            onClicked: assetRow.remove()
        }
    }

    // A box on the canvas: click selects, drag moves, the corner handle resizes; both
    // snap to the screen and the other elements and count as one undo step.
    component ElementBox: Item {
        id: box

        required property int index
        readonly property var spec: page.elements[index] ?? ({})
        readonly property bool current: page.selected === index

        x: (spec.x ?? 0) / 100 * monitor.width
        y: (spec.y ?? 0) / 100 * monitor.height
        width: (spec.width ?? 0) / 100 * monitor.width
        height: (spec.height ?? 0) / 100 * monitor.height

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: box.current ? 2 : 1
            border.color: box.current ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, mover.containsMouse ? 0.7 : 0.25)
        }
        MouseArea {
            id: mover

            property point start
            property point startValue

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeAllCursor
            onPressed: mouse => {
                canvas.forceActiveFocus();
                page.selected = box.index;
                start = mapToItem(monitor, mouse.x, mouse.y);
                startValue = Qt.point(box.spec.x, box.spec.y);
                page.editor.beginGesture();
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return;
                const p = mapToItem(monitor, mouse.x, mouse.y);
                const snapped = page.editor.snapMove(box.index, startValue.x + (p.x - start.x) / monitor.width * 100,
                                                     startValue.y + (p.y - start.y) / monitor.height * 100,
                                                     page.tolerance / monitor.width * 100, page.tolerance / monitor.height * 100);
                page.guideX = snapped.guideX;
                page.guideY = snapped.guideY;
                page.editor.setElement(box.index, { x: snapped.x, y: snapped.y });
            }
            onReleased: page.endDrag()
            onCanceled: page.endDrag()
        }
        Rectangle {
            visible: box.current
            width: Kirigami.Units.smallSpacing * 3
            height: width
            x: parent.width - width / 2
            y: parent.height - height / 2
            color: Kirigami.Theme.highlightColor

            MouseArea {
                property point start
                property size startSize

                anchors.fill: parent
                anchors.margins: -Kirigami.Units.smallSpacing
                cursorShape: Qt.SizeFDiagCursor
                onPressed: mouse => {
                    start = mapToItem(monitor, mouse.x, mouse.y);
                    startSize = Qt.size(box.spec.width, box.spec.height);
                    page.editor.beginGesture();
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    const p = mapToItem(monitor, mouse.x, mouse.y);
                    const snapped = page.editor.snapResize(box.index, startSize.width + (p.x - start.x) / monitor.width * 100,
                                                           startSize.height + (p.y - start.y) / monitor.height * 100,
                                                           page.tolerance / monitor.width * 100, page.tolerance / monitor.height * 100);
                    page.guideX = snapped.guideX;
                    page.guideY = snapped.guideY;
                    page.editor.setElement(box.index, { width: snapped.width, height: snapped.height });
                }
                onReleased: page.endDrag()
                onCanceled: page.endDrag()
            }
        }
    }

    // Our own picker: Qt's ColorDialog showed a blank square in the KDE style (issue #4).
    Kirigami.Dialog {
        id: colorDialog

        property ColorButton target: null

        title: qsTr("Pick a Colour")
        preferredWidth: Kirigami.Units.gridUnit * 22
        padding: Kirigami.Units.largeSpacing
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: target?.picked(colorPicker.picked)

        ColorPicker {
            id: colorPicker
        }
    }
    FileDialog {
        id: imageDialog

        property string purpose // add, replace, background

        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.svg)")]
        onAccepted: {
            const added = page.editor.addImage(selectedFile);
            if (added.error)
                page.message = added.error;
            else if (purpose === "background")
                page.editor.setBackground({ image: added.hash });
            else if (purpose === "replace")
                page.editor.setElement(page.selected, { asset: added.hash });
            else if (purpose === "shared")
                page.message = page.editor.setSharedLogo(page.element.shared, added.hash);
            else if (purpose.startsWith("icon:")) {
                const icons = Object.assign({}, page.editor.content.icons ?? {});
                icons[purpose.slice(5)] = added.hash;
                page.editor.setContent({ icons: icons });
            }
            else
                page.selected = page.editor.addElement("image", { asset: added.hash });
        }
    }

    // Converted to a silent MP4 in the background (SPEC.md §4.7); the editor adds it
    // when done, at the bottom of the element list.
    FileDialog {
        id: videoDialog

        property string purpose // add, replace

        nameFilters: [qsTr("Videos (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.gif)")]
        onAccepted: {
            page.pendingAdd = purpose === "add" ? "video" : "";
            page.message = page.editor.addVideo(selectedFile, purpose === "replace" ? page.selected : -1);
        }
    }

    // Boot splash animations (SPEC.md §4.6).
    FileDialog {
        id: framesImageDialog

        property int replaceIndex: -1

        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images, one per frame (*.png *.jpg *.jpeg *.webp *.svg)")]
        onAccepted: {
            page.message = page.editor.addImageFrames(selectedFiles, replaceIndex);
            if (page.message === "" && replaceIndex < 0)
                page.selected = page.elements.length - 1;
        }
    }
    FileDialog {
        id: framesVideoDialog

        property int replaceIndex: -1

        nameFilters: [qsTr("Videos (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.gif)")]
        onAccepted: framesDialog.start(selectedFile, replaceIndex)
    }
    // Video → frames: shows what the animation costs before it is built.
    Kirigami.Dialog {
        id: framesDialog

        property url file
        property int replaceIndex: -1
        property var plan: ({})
        property real bytesPerFrame: -1 // measured from the first second; -1 while measuring
        readonly property int frames: Math.max(1, Math.round(fpsField.value * lengthField.number))
        readonly property real frameMemory: (plan.frameWidth ?? 0) * (plan.frameHeight ?? 0) * 4

        function start(url, replace) {
            plan = page.editor.videoPlan(url, replace);
            if (plan.error) {
                page.message = plan.error;
                return;
            }
            file = url;
            replaceIndex = replace;
            fpsField.value = Math.min(30, Math.round(plan.fps));
            lengthField.number = Math.min(plan.seconds, 2);
            measure();
            open();
        }
        function measure() {
            bytesPerFrame = -1;
            page.editor.measureFrames(file, plan.frameWidth, plan.frameHeight, fpsField.value);
        }

        title: qsTr("Animation from Video")
        standardButtons: Kirigami.Dialog.NoButton
        preferredWidth: Kirigami.Units.gridUnit * 28
        padding: Kirigami.Units.largeSpacing
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Convert")
                icon.name: "dialog-ok-apply"
                onTriggered: {
                    framesDialog.close();
                    page.pendingAdd = framesDialog.replaceIndex < 0 ? "frames" : "";
                    page.message = page.editor.addVideoFrames(framesDialog.file, framesDialog.plan.frameWidth, framesDialog.plan.frameHeight,
                                                              fpsField.value, lengthField.number, framesDialog.replaceIndex);
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: framesDialog.close()
            }
        ]

        Connections {
            target: page.editor
            function onFramesMeasured(bytes) {
                framesDialog.bytesPerFrame = bytes;
            }
        }

        Kirigami.FormLayout {
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Video:")
                text: qsTr("%1×%2, %3 s at %4 fps").arg(framesDialog.plan.width ?? 0).arg(framesDialog.plan.height ?? 0)
                    .arg((framesDialog.plan.seconds ?? 0).toFixed(1)).arg(Math.round(framesDialog.plan.fps ?? 0))
            }
            QQC2.SpinBox {
                id: fpsField
                Kirigami.FormData.label: qsTr("Frames per second:")
                from: 1
                to: 30 // two-step draws at 30 fps
                onValueModified: framesDialog.measure()
            }
            DecimalField {
                id: lengthField
                Kirigami.FormData.label: qsTr("Length (s):")
                from: 10
                to: Math.round((framesDialog.plan.seconds ?? 0) * 100)
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Frames:")
                text: qsTr("%1 of %2×%3 px; %4").arg(framesDialog.frames).arg(framesDialog.plan.frameWidth ?? 0).arg(framesDialog.plan.frameHeight ?? 0)
                    .arg(Math.abs(lengthField.number - 2) < 0.01 ? qsTr("plays with two-step") : qsTr("plays with the script module"))
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("Memory:")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("%1 MB; up to %2 s fit in the %3 MB left")
                    .arg(page.megabytes(framesDialog.frames * framesDialog.frameMemory))
                    .arg((framesDialog.plan.memoryLeft / Math.max(1, framesDialog.frameMemory * fpsField.value)).toFixed(1))
                    .arg(page.megabytes(framesDialog.plan.memoryLeft ?? 0))
            }
            QQC2.Label {
                Kirigami.FormData.label: qsTr("File size:")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: framesDialog.bytesPerFrame < 0 ? qsTr("Measuring…")
                    : qsTr("about %1 MB; up to %2 s fit in the %3 MB left")
                          .arg(page.megabytes(framesDialog.frames * framesDialog.bytesPerFrame))
                          .arg((framesDialog.plan.bytesLeft / Math.max(1, framesDialog.bytesPerFrame * fpsField.value)).toFixed(1))
                          .arg(page.megabytes(framesDialog.plan.bytesLeft ?? 0))
            }
        }
    }
    Connections {
        target: page.editor
        function onConversionFinished(error) {
            if (error)
                page.message = error;
            else if (page.pendingAdd === "video" && page.elements[0]?.type === "video")
                page.selected = 0;
            else if (page.pendingAdd === "frames" && page.elements[page.elements.length - 1]?.type === "frames")
                page.selected = page.elements.length - 1;
            page.pendingAdd = "";
        }
    }

    Kirigami.PromptDialog {
        id: shareDialog

        title: qsTr("Share as a Logo")
        subtitle: qsTr("Other designs can use it too. Changing it later changes all of them, once they are applied again.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onOpened: shareName.text = page.element ? page.label(page.element) : ""
        onAccepted: page.message = page.editor.shareLogo(page.selected, shareName.text)

        QQC2.TextField {
            id: shareName
            placeholderText: qsTr("Name")
        }
    }
    Kirigami.Dialog {
        id: logoDialog

        title: qsTr("Shared Logos")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 20

        ColumnLayout {
            spacing: 0
            Repeater {
                model: page.sharedLogos
                delegate: QQC2.ItemDelegate {
                    required property string modelData
                    Layout.fillWidth: true
                    text: qsTr("%1 (in %n design(s))", "", page.editor.shared[modelData]?.uses ?? 0).arg(page.editor.shared[modelData]?.name ?? "")
                    icon.source: page.editor.assets[page.editor.shared[modelData]?.asset]?.url ?? ""
                    icon.color: "transparent"
                    onClicked: {
                        page.selected = page.editor.addSharedLogo(modelData);
                        logoDialog.close();
                    }
                }
            }
        }
    }

    Kirigami.Dialog {
        id: versionsDialog

        title: qsTr("Versions of “%1”").arg(page.editor.name)
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 24
        padding: Kirigami.Units.largeSpacing

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: page.editor.versions
                delegate: RowLayout {
                    id: versionRow

                    required property var modelData

                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: qsTr("Version %1 · %2").arg(versionRow.modelData.version).arg(versionRow.modelData.savedAt.toLocaleString(Qt.locale(), Locale.ShortFormat))
                    }
                    QQC2.Button {
                        text: qsTr("Open")
                        onClicked: {
                            page.editor.restoreVersion(versionRow.modelData.version);
                            versionsDialog.close();
                        }
                    }
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTr("Opening a version puts its content into the editor (Undo takes it back); saving makes it the newest version. Every version is kept.")
            }
        }
    }

    Kirigami.PromptDialog {
        id: deletePaletteDialog

        title: qsTr("Delete Palette?")
        subtitle: qsTr("“%1” is removed from the library. This design keeps its colours as its own.").arg(page.editor.shared[page.paletteId]?.name ?? "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    deletePaletteDialog.close();
                    page.message = page.editor.deletePalette(page.paletteId);
                }
            }
        ]
    }

    Kirigami.PromptDialog {
        id: leaveDialog

        title: qsTr("Save Changes?")
        subtitle: qsTr("“%1” has changes that aren't saved.").arg(page.editor.name)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Save")
                icon.name: "document-save"
                onTriggered: {
                    leaveDialog.close();
                    if (page.saveDesign()) {
                        page.editor.close();
                        page.leave();
                    }
                }
            },
            Kirigami.Action {
                text: qsTr("Discard")
                icon.name: "edit-delete"
                onTriggered: {
                    leaveDialog.close();
                    page.editor.discard();
                    page.editor.close();
                    page.leave();
                }
            },
            Kirigami.Action {
                text: qsTr("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: leaveDialog.close()
            }
        ]
    }
}
