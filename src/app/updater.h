#pragma once

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Updates from GitHub releases (SPEC.md §4.14, ARCHITECTURE.md §9c).

// A release as GitHub's "latest release" API describes it.
struct Release {
    QString version; // "0.5.0", from the tag "v0.5.0"
    QString notes; // the release text (Markdown)
    QUrl page; // the release on github.com
    QUrl deb; // the ekpo_<version>_amd64.deb asset
    QUrl sums; // the SHA256SUMS asset
};

// Whether version a is newer than b ("0.10.0" > "0.9.3"); a leading "v" is ignored and
// anything that isn't a version is never newer.
bool isNewerVersion(const QString &a, const QString &b);
// The release in that JSON, or nothing and why not (e.g. no .deb among its assets).
std::optional<Release> parseRelease(const QByteArray &json, QString &error);
// Checks a downloaded package: its line in SHA256SUMS matches and dpkg-deb reads
// that version from it. Empty if it's fine, else why not.
QString verifyPackage(const QString &debPath, const QByteArray &sums, const QString &version);

// Where updates come from and how they're installed: GitHub and "pkexec apt-get
// install -y" in packages. Dev builds (EKPO_DEV) only check EKPO_UPDATE_URL (unset:
// never) and install through tests/stubs/install-update.
struct UpdateSource {
    QUrl releaseUrl; // empty: never check
    QString installer;
    QStringList installerArguments; // the .deb path follows them
};
UpdateSource updateSource();

// After an update: main() starts Ekpo again once it has let go of the single-instance
// socket, from the path of the new binary.
void requestRestart();
bool restartRequested();
QString restartPath();

class Updater : public QObject
{
    Q_OBJECT
public:
    Updater(const UpdateSource &source, const QString &currentVersion, const QString &cacheDir, QObject *parent = nullptr);
    ~Updater() override;

    bool canCheck() const { return !m_source.releaseUrl.isEmpty(); }
    bool busy() const;
    // Asks for the newest release; emits checked.
    void check();
    // Downloads the release into the cache folder, checks it and installs it; emits
    // progress, then finished.
    void update(const Release &release);
    // Stops a download; finished reports it with an empty error.
    void cancel();

Q_SIGNALS:
    // found: a release newer than the running version. error: the check failed.
    void checked(bool found, const Release &release, const QString &error);
    // fraction: 0–1, or -1 while unknown.
    void progress(const QString &text, double fraction);
    // ok: installed. Not ok with an empty error: cancelled.
    void finished(bool ok, const QString &error);

private:
    void fail(const QString &error);
    void downloadPackage();
    void install();

    UpdateSource m_source;
    QString m_currentVersion;
    QString m_cacheDir;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QProcess *m_process = nullptr;
    Release m_release;
    QByteArray m_sums;
    QFile m_file;
    bool m_updating = false;
};
