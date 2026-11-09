#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QCloseEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QLabel;

namespace bld::ui {

// Mirrors BlueBrick's two-step Download Center workflow:
//   Step 1 — LibraryPackageSourceForm: user ticks one or more sources
//            (Official, Non-LEGO, Custom URL), we fetch the HTML index
//            page for each and regex-parse `<a href="X.zip">X.zip</a>`
//            entries to build a package list. Each package is shown as
//            new, as an update ("Update 1.2 → 1.3") or as Included when
//            any parts folder already has that version or a newer one.
//   Step 2 — DownloadCenterForm: the user ticks which to install; each
//            is downloaded to a temporary file (with a size cap, a stall
//            timeout and Cancel), unpacked beside the library and only
//            then moved into <libraryRoot>/, so a bad archive leaves
//            nothing half-installed. The dialog stays open afterwards.
class DownloadCenterDialog : public QDialog {
    Q_OBJECT
public:
    // libraryRoot is where packages are installed (AppData/parts).
    // libraryFolders are every parts folder the app reads (the bundled
    // library included), checked for what's already installed.
    explicit DownloadCenterDialog(QString libraryRoot, QStringList libraryFolders = {},
                                  QWidget* parent = nullptr);
    ~DownloadCenterDialog() override;

    QString libraryRoot() const { return libraryRoot_; }
    // Packages installed while the dialog was open (however it closed).
    int     installedCount() const { return installedCount_; }

    // Biggest package accepted, in bytes (tests lower it).
    void setMaxPackageBytes(qint64 bytes) { maxPackageBytes_ = bytes; }
    // How long a download may go without receiving anything (tests lower it).
    void setStallTimeoutMs(int ms) { stallTimeoutMs_ = ms; }

public slots:
    // While downloading, Esc and Close stop the download instead.
    void reject() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onSearchClicked();
    void onCustomToggled(bool on);
    void onDownloadClicked();

private:
    struct Package {
        QString name;          // basename without version, e.g. "LegoTrains"
        QString version;       // version part of filename (may be empty)
        QString sourceUrl;     // full http(s) URL to the .zip
        QString fileName;      // bare filename ("X.zip")
    };

    // Fetch one URL (with a per-request timeout) and return its body as
    // text. Sets `error` on failure, returns empty.
    QString fetchUrl(const QUrl& url, QString* error);

    // Every parts folder's packages: name (case-folded) -> newest version
    // there (from config/About.txt).
    QHash<QString, QString> readInstalledVersions() const;

    // Download one package's zip to a temporary file, unpack it beside
    // libraryRoot_, then move it into place. Returns true on success, sets
    // `error` otherwise (nothing is left behind on failure).
    bool downloadAndInstall(const Package& pkg, QString* error);
    bool unpackAndMove(const QByteArray& zipBytes, QString* error);
    void cancelDownload();

    QString          libraryRoot_;
    QStringList libraryFolders_;
    int              installedCount_ = 0;
    qint64 maxPackageBytes_;
    int stallTimeoutMs_;
    bool downloading_ = false;
    bool cancelled_ = false;
    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* current_ = nullptr;  // the download going, only while downloadAndInstall() runs

    // ---- Source selection ----
    QCheckBox*  cbOfficial_   = nullptr;
    QCheckBox*  cbNonLego_    = nullptr;
    QCheckBox*  cbCustom_     = nullptr;
    QLineEdit*  customUrl_    = nullptr;
    QPushButton* searchBtn_   = nullptr;
    QLabel*     statusLabel_  = nullptr;

    // ---- Candidate list (populated after search) ----
    QListWidget* candidates_  = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* closeBtn_    = nullptr;
    QProgressBar* progress_   = nullptr;
};

}  // namespace bld::ui
