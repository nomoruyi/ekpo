#pragma once

#include "gengrub.h"
#include "genplymouth.h"
#include "library.h"
#include "media.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

// The design open in the editor (SPEC.md §4.4): edits with undo and redo, a version
// per save, the crash draft, the preview and its warnings. Plasma splash and boot
// splash designs; their content is the one genksplash.h and genplymouth.h describe.
class DesignEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Backend.editor is the one editor")

    Q_PROPERTY(bool isOpen READ isOpen NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed) // "plasmaSplash" or "bootSplash"
    Q_PROPERTY(QString designId READ designId NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QVariantMap content READ content NOTIFY changed)
    Q_PROPERTY(int version READ version NOTIFY changed) // 0 = never saved
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool recovered READ recovered NOTIFY changed) // opened from the crash draft
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed) // {version, savedAt, thumbnail}
    Q_PROPERTY(QVariantMap assets READ assets NOTIFY changed) // hash → {name, kind, url}
    // Shared logos and palettes (SPEC.md §3): id → {kind: "logo"|"palette", name, asset,
    // colours: {background, accent, text}, uses: designs that link it}
    Q_PROPERTY(QVariantMap shared READ shared NOTIFY changed)
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY previewChanged) // Plasma splash: the built Splash.qml
    // Boot splash: what Plymouth draws, for PlymouthPreview.qml, and the meters:
    // {background, frames: [url], seconds, centreX, centreY, module, hideMessages,
    //  progress: {x, y, width, height, color, background} (pixels on the built screen),
    //  bytes, capBytes, memory, budgetBytes, builtWidth, builtHeight}
    Q_PROPERTY(QVariantMap plymouth READ plymouth NOTIFY previewChanged)
    // Boot menu: {preview: the menu drawn as GRUB will (a PNG), fits}, built for previewScreen.
    Q_PROPERTY(QVariantMap bootMenu READ bootMenu NOTIFY previewChanged)
    // The entry classes GRUB looks up icons for ("kubuntu", "windows", ...).
    Q_PROPERTY(QStringList iconClasses READ iconClasses NOTIFY previewChanged)
    Q_PROPERTY(QStringList problems READ problems NOTIFY previewChanged) // block applying
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY previewChanged) // per resolution
    Q_PROPERTY(double conversionProgress READ conversionProgress NOTIFY conversionChanged) // 0..1, -1 = none running
    Q_PROPERTY(QString conversionName READ conversionName NOTIFY conversionChanged)

public:
    explicit DesignEditor(QObject *parent = nullptr);
    ~DesignEditor() override; // writes a pending draft

    void setLibrary(Library *library); // closes the open design

    bool isOpen() const { return m_open; }
    QString stage() const { return m_open ? stageKey(m_stage) : QString(); }
    QString designId() const { return m_id; }
    QString name() const { return m_current.name; }
    void setName(const QString &name);
    QVariantMap content() const { return m_current.content.toVariantMap(); }
    int version() const { return m_version; }
    bool dirty() const { return m_open && (m_version == 0 || !(m_current == m_saved)); }
    bool recovered() const { return m_recovered; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QVariantList versions() const { return m_versions; }
    QVariantMap assets() const { return m_assets; }
    QVariantMap shared() const { return m_shared; }
    QUrl previewUrl() const { return m_previewUrl; }
    QVariantMap plymouth() const { return m_plymouth; }
    QVariantMap bootMenu() const { return m_bootMenu; }
    QStringList iconClasses() const { return menuClasses(menuEntries()); }
    // What the boot menu preview lists and is checked against: the probe's expected
    // entries (Backend sets them), or a sample.
    void setMenuEntries(const QStringList &entries);
    QStringList menuEntries() const;
    // The screen the boot menu preview is built for (the editor's resolution switch).
    Q_INVOKABLE void setPreviewScreen(int width, int height);
    // Boot menu icons for every class the menu shows: Ekpo's built-in ones, or those an
    // imported GRUB theme has in icons/ (stored as assets). Empty on success.
    Q_INVOKABLE void useBuiltinIcons();
    Q_INVOKABLE QString useImportIcons(const QString &importId);
    QStringList problems() const { return m_problems; }
    QStringList warnings() const { return m_warnings; }
    QString previewPackage() const { return m_previewPackage; } // folder of the current preview build
    double conversionProgress() const { return m_job ? m_job->progress() : -1; }
    QString conversionName() const { return m_job ? m_jobName : QString(); }

    // Opens a saved design, or its crash draft if there is one. Empty on success.
    Q_INVOKABLE QString open(const QString &id);
    // A new design: empty, or a starter (starters()) tinted with accent (the Plasma
    // accent colour).
    Q_INVOKABLE void create(const QString &stage = QStringLiteral("plasmaSplash"), const QString &starter = {}, const QColor &accent = {});
    // A new, never saved copy of a design's current version, links included.
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE QString save(); // the next version; empty on success
    // Where the picture of the version just saved goes (QML grabs the canvas), and the
    // call after it is written, so Versions… shows it.
    Q_INVOKABLE QString thumbnailFile() const;
    Q_INVOKABLE void thumbnailSaved();
    Q_INVOKABLE void close(); // keeps the draft of unsaved changes; cancels a video conversion
    // Drops unsaved changes and the draft; a design that was never saved closes.
    Q_INVOKABLE void discard();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // A drag: every change until endGesture() is one undo step, and the preview
    // waits for the end.
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();

    // Merge values into the content, the background or an element. A null value
    // removes the key; colours become "#rrggbb" or "#aarrggbb".
    Q_INVOKABLE void setContent(const QVariantMap &values);
    Q_INVOKABLE void setBackground(const QVariantMap &values);
    Q_INVOKABLE void setElement(int index, const QVariantMap &values);
    // "text", "image", "spinner", "video" (Plasma splash), "frames" or "progress"
    // (boot splash) with defaults. An image or frames without a size get one that
    // keeps their aspect ratio; a video fills the screen and goes first (below
    // everything else). Returns the new index.
    Q_INVOKABLE int addElement(const QString &type, const QVariantMap &values = {});
    Q_INVOKABLE void removeElement(int index);
    // A copy right after it, a little lower and to the right; the new index, or -1
    // (a boot menu has one menu). Ctrl+D in the editor.
    Q_INVOKABLE int duplicateElement(int index);
    // The element's look without its content or place: colours, font, size, alignment,
    // styles and effects. Pasted onto the same type all of it applies, onto another only
    // what every type shares (colour, font, size, alignment, text effects).
    Q_INVOKABLE QVariantMap elementStyle(int index) const;
    Q_INVOKABLE void pasteStyle(int index, const QVariantMap &style);
    Q_INVOKABLE void moveElement(int from, int to);
    // Replaces the content and name with a saved version's, as one undoable edit.
    Q_INVOKABLE void restoreVersion(int version);

    // Into the asset library: {hash} or {error}.
    Q_INVOKABLE QVariantMap addImage(const QUrl &file);
    Q_INVOKABLE QVariantMap addSystemFont(const QString &family); // located through fontconfig
    // Converts a video to a silent MP4 of at most 1440p in the background (one at a
    // time), stores it as an asset and adds a video element, or puts it into the
    // video element at replaceIndex. Only the segment from start, seconds long (0: to
    // the end), so one recording gives several clips. Empty if the conversion started.
    Q_INVOKABLE QString addVideo(const QUrl &file, int replaceIndex = -1, double start = 0, double seconds = 0);
    Q_INVOKABLE void cancelConversion();

    // Boot splash animations (SPEC.md §4.6). From images: sorted by file name, each
    // stored as an asset; a frames element is added, or the one at replaceIndex gets
    // them. Empty on success.
    Q_INVOKABLE QString addImageFrames(const QList<QUrl> &files, int replaceIndex = -1);
    // From a video, in two steps. videoPlan: the source and the frame size the box
    // gives, and what the theme has left for an animation:
    // {width, height, fps, seconds, frameWidth, frameHeight, memoryLeft, bytesLeft} or {error}.
    Q_INVOKABLE QVariantMap videoPlan(const QUrl &file, int replaceIndex = -1) const;
    // Converts the first second from start in the background and reports the bytes per
    // frame (framesMeasured; -1 if it failed), for the file size before the real conversion.
    Q_INVOKABLE void measureFrames(const QUrl &file, int width, int height, double fps, double start = 0);
    // The real conversion of seconds from start, like addVideo (progress, cancel); then
    // like addImageFrames.
    Q_INVOKABLE QString addVideoFrames(const QUrl &file, int width, int height, double fps, double seconds, int replaceIndex = -1, double start = 0);

    // Shared logos: share an image element's image under a name (the element links it),
    // add an element that links one, change one (every design that links it changes),
    // or unlink an element (it keeps the image it shows now). Empty on success.
    Q_INVOKABLE QString shareLogo(int index, const QString &name);
    Q_INVOKABLE int addSharedLogo(const QString &id);
    Q_INVOKABLE QString setSharedLogo(const QString &id, const QString &asset);
    Q_INVOKABLE void unlink(int index);
    // Shared palettes: colours "@background", "@accent" and "@text" are the linked
    // palette's. newPalette makes one from this design's colours and links it;
    // linkPalette("") unlinks (palette colours become the design's own).
    Q_INVOKABLE QString newPalette(const QString &name);
    Q_INVOKABLE void linkPalette(const QString &id);
    // A palette from an image's main colours (gencommon.h), linked to the open design.
    Q_INVOKABLE QString newPaletteFromImage(const QUrl &file);
    // Deletes a palette; the open design keeps its colours as its own. Refused while
    // another design, a saved version or a draft links it. Empty on success.
    Q_INVOKABLE QString deletePalette(const QString &id);
    Q_INVOKABLE QString setPaletteColour(const QString &key, const QColor &colour);

    // Snapping (positions and tolerances in % of the screen): the box's edges or
    // centre line meet a screen edge, the screen centre, or another element's edge or
    // centre. Returns {x, y, guideX, guideY} or {width, height, guideX, guideY};
    // a guide is -1 when nothing snapped on that axis.
    Q_INVOKABLE QVariantMap snapMove(int index, double x, double y, double toleranceX, double toleranceY) const;
    Q_INVOKABLE QVariantMap snapResize(int index, double width, double height, double toleranceX, double toleranceY) const;

    // ksplashqml --test --window on the preview package. Empty on success.
    Q_INVOKABLE QString runRealSplash() const;

    void flushPreview(); // builds the preview now (tests)
    void flushDraft(); // writes the draft now (tests)

Q_SIGNALS:
    void changed();
    void previewChanged();
    void libraryChanged(); // saved, or a draft appeared or went away
    void conversionChanged();
    void conversionFinished(const QString &error); // empty after success or cancel
    void framesMeasured(double bytesPerFrame);

private:
    struct State {
        QString name;
        QJsonObject content;
        bool operator==(const State &o) const { return name == o.name && content == o.content; }
    };

    void load(const QString &id, Stage stage, int version, const State &state, const State &saved, bool recovered);
    void edit(const std::function<void(State &)> &change);
    Design currentDesign() const;
    void reloadLists();
    QJsonObject element(int index) const;
    QString startJob(MediaJob *job, const QString &name, const std::function<QString()> &done);
    void finishConversion(bool ok, const QString &error);
    // Stores frame files as assets and puts them into a frames element.
    QString putFrames(const QStringList &files, const QString &name, const QString &source, double seconds, int replaceIndex);
    QString saveShared(SharedElement &element); // assigns a new one's id; rebuilds the preview
    void flushBootMenuPreview(const Design &design, QStringList problems);
    void keepPreviews(const QString &current); // removes older preview builds

    Library *m_library = nullptr;
    bool m_open = false;
    Stage m_stage = Stage::PlasmaSplash;
    QString m_id;
    int m_version = 0;
    State m_current;
    State m_saved;
    bool m_recovered = false;
    bool m_draftWritten = false;
    QList<State> m_undo;
    QList<State> m_redo;
    bool m_gesture = false;
    bool m_gestureRecorded = false;
    QVariantList m_versions;
    QVariantMap m_assets;
    QVariantMap m_shared;

    QTimer m_draftTimer;
    QTimer m_previewTimer;
    QTemporaryDir m_previewDir;
    QString m_previewPackage;
    QUrl m_previewUrl;
    QVariantMap m_plymouth;
    QVariantMap m_bootMenu;
    QStringList m_menuEntries;
    QSize m_previewScreen = builtScreen;
    PlymouthLayout m_layout; // of the last boot splash preview
    QStringList m_problems;
    QStringList m_warnings;

    // Video conversions for the open design; done() puts the result in.
    QTemporaryDir m_mediaDir;
    std::unique_ptr<MediaJob> m_job;
    QString m_jobName;
    std::function<QString()> m_jobDone;
    std::unique_ptr<MediaJob> m_sample; // measureFrames
};
