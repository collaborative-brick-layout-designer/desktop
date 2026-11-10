#include "DownloadCenterDialog.h"
#include "../import/ArchivePath.h"
#include "../parts/PartsLibrary.h"
#include "help/HelpButton.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "../import/zip/SafeZip.h"

#include <algorithm>

namespace bld::ui {

namespace {

// BlueBrick's hard-coded sources. Same URLs vanilla uses; the host
// serves an Apache-style HTML directory listing of .zip files.
constexpr const char* kOfficialUrl =
    "https://bluebrick.lswproject.com/download/package/";
constexpr const char* kNonLegoUrl =
    "https://bluebrick.lswproject.com/download/packageOther/";

constexpr int kMaxFetchBytes  = 4 * 1024 * 1024;    // search-page HTML cap
constexpr qint64 kMaxPackageBytes = qint64(100) * 1024 * 1024; // per-zip cap
constexpr int kFetchTimeoutMs = 20000;
constexpr int kStallTimeoutMs = 60000; // a download that receives nothing this long stops

// Row data roles.
constexpr int kUrlRole = Qt::UserRole + 0, kFileRole = Qt::UserRole + 1, kNameRole = Qt::UserRole + 2,
              kVersionRole = Qt::UserRole + 3;

const QString kUserAgent = QStringLiteral(
    "Mozilla/5.0 (compatible; BLD/1.0; +https://github.com/collaborative-brick-layout-designer/desktop)");

}  // namespace

DownloadCenterDialog::DownloadCenterDialog(QString libraryRoot, QStringList libraryFolders, QWidget* parent)
    : QDialog(parent), libraryRoot_(std::move(libraryRoot)), libraryFolders_(std::move(libraryFolders)),
      maxPackageBytes_(kMaxPackageBytes), stallTimeoutMs_(kStallTimeoutMs),
      nam_(new QNetworkAccessManager(this)) {
    if (!libraryFolders_.contains(libraryRoot_)) libraryFolders_ << libraryRoot_;
    setWindowTitle(tr("Download Center"));
    resize(640, 480);

    auto* root = new QVBoxLayout(this);

    // ---- Source selection group (mirrors LibraryPackageSourceForm) ----
    auto* srcGroup = new QGroupBox(tr("Look for part packs on:"), this);
    root->addWidget(
        help::withHelp(new QLabel(tr("Packs of parts made for BlueBrick. Search, tick the ones you want, "
                                     "then install them."),
                                  this),
                       QStringLiteral("parts.getMore"), this));
    auto* srcLayout = new QVBoxLayout(srcGroup);
    cbOfficial_ = new QCheckBox(tr("BlueBrick packs (Town, Train, Castle, BrickTracks, TrixBrix, …)"), srcGroup);
    cbOfficial_->setChecked(true);
    cbOfficial_->setToolTip(QString::fromLatin1(kOfficialUrl));
    cbNonLego_ = new QCheckBox(tr("Other toy brands (Plarail)"), srcGroup);
    cbNonLego_->setToolTip(QString::fromLatin1(kNonLegoUrl));
    cbCustom_ = new QCheckBox(tr("Unofficial location (custom URL)"), srcGroup);
    customUrl_ = new QLineEdit(srcGroup);
    customUrl_->setPlaceholderText(tr("https://example.com/parts/"));
    customUrl_->setEnabled(false);
    {
        QSettings s;
        const QString lastCustom = s.value(QStringLiteral("download/customUrl")).toString();
        if (!lastCustom.isEmpty()) customUrl_->setText(lastCustom);
    }
    srcLayout->addWidget(cbOfficial_);
    srcLayout->addWidget(cbNonLego_);
    {
        auto* row = new QHBoxLayout();
        row->addWidget(cbCustom_);
        row->addWidget(customUrl_, 1);
        srcLayout->addLayout(row);
    }
    root->addWidget(srcGroup);

    // ---- Search button + status ----
    {
        auto* row = new QHBoxLayout();
        searchBtn_ = new QPushButton(tr("Search"), this);
        statusLabel_ = new QLabel(this);
        statusLabel_->setText(tr("No search yet."));
        row->addWidget(searchBtn_);
        row->addWidget(statusLabel_, 1);
        root->addLayout(row);
    }

    // ---- Candidate list ----
    auto* listLabel = new QLabel(tr("Part packs (the ones you already have say Included):"), this);
    root->addWidget(listLabel);
    candidates_ = new QListWidget(this);
    candidates_->setAccessibleName(tr("Part packs you can get"));
    listLabel->setBuddy(candidates_);
    candidates_->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(candidates_, 1);

    // ---- Progress + footer ----
    progress_ = new QProgressBar(this);
    progress_->setVisible(false);
    root->addWidget(progress_);

    {
        auto* row = new QHBoxLayout();
        row->addStretch(1);
        downloadBtn_ = new QPushButton(tr("Download && Install"), this);
        downloadBtn_->setEnabled(false);
        cancelBtn_ = new QPushButton(tr("Cancel Download"), this);
        cancelBtn_->setVisible(false);
        closeBtn_ = new QPushButton(tr("Close"), this);
        row->addWidget(downloadBtn_);
        row->addWidget(cancelBtn_);
        row->addWidget(closeBtn_);
        root->addLayout(row);
    }

    connect(cbCustom_, &QCheckBox::toggled, this, &DownloadCenterDialog::onCustomToggled);
    connect(searchBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::onSearchClicked);
    connect(downloadBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::onDownloadClicked);
    connect(cancelBtn_, &QPushButton::clicked, this, &DownloadCenterDialog::cancelDownload);
    // Close: what was installed stays installed (installedCount()).
    connect(closeBtn_, &QPushButton::clicked, this, [this] { installedCount_ > 0 ? accept() : reject(); });
}

// A download still going is the network manager's child, so it goes with the dialog.
DownloadCenterDialog::~DownloadCenterDialog() = default;

void DownloadCenterDialog::cancelDownload() {
    cancelled_ = true;
    if (current_) current_->abort();
}

void DownloadCenterDialog::closeEvent(QCloseEvent* e) {
    // Closing the window while it downloads stops the download first.
    if (downloading_) {
        cancelDownload();
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}

void DownloadCenterDialog::reject() {
    if (downloading_) {
        cancelDownload();
        return;
    }
    QDialog::reject();
}

void DownloadCenterDialog::onCustomToggled(bool on) {
    customUrl_->setEnabled(on);
}

QString DownloadCenterDialog::fetchUrl(const QUrl& url, QString* error) {
    QNetworkRequest req(url);
    // Match BlueBrick's user-agent so servers that filter on UA still
    // serve us a directory listing.
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply* rep = nam_->get(req);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (rep->error() != QNetworkReply::NoError) {
        if (error) *error = rep->errorString();
        rep->deleteLater();
        return {};
    }
    const QByteArray bytes = rep->read(kMaxFetchBytes + 1);
    rep->deleteLater();
    if (bytes.size() > kMaxFetchBytes) {
        if (error) *error = tr("Search response too large.");
        return {};
    }
    return QString::fromUtf8(bytes);
}

QHash<QString, QString> DownloadCenterDialog::readInstalledVersions() const {
    // Every parts folder, the bundled library included: a pack the app came
    // with used to be offered as new. BlueBrick keeps the version at
    // <pkg>/config/About.txt (`version=X.Y`), so packs made for vanilla
    // work here too.
    QHash<QString, QString> out;
    for (const QString& folder : libraryFolders_) {
        const QFileInfoList dirs = QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& d : dirs) {
            const QString version = parts::PartsLibrary::packageVersion(d.absoluteFilePath());
            const QString name = d.fileName().toLower();
            const auto it = out.constFind(name);
            if (it == out.constEnd() || parts::PartsLibrary::comparePackageVersions(version, it.value()) > 0)
                out.insert(name, version);
        }
    }
    return out;
}

void DownloadCenterDialog::onSearchClicked() {
    QStringList urls;
    if (cbOfficial_->isChecked()) urls.append(QString::fromLatin1(kOfficialUrl));
    if (cbNonLego_->isChecked())  urls.append(QString::fromLatin1(kNonLegoUrl));
    if (cbCustom_->isChecked()) {
        QString u = customUrl_->text().trimmed();
        if (!u.isEmpty()) {
            // Ensure trailing slash so relative <a href="X.zip"> resolves
            // against a directory base, matching how BlueBrick treats
            // these URLs.
            if (!u.endsWith(QChar('/'))) u.append(QChar('/'));
            urls.append(u);
            QSettings().setValue(QStringLiteral("download/customUrl"), u);
        }
    }
    if (urls.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
            tr("Pick at least one source to search."));
        return;
    }

    candidates_->clear();
    downloadBtn_->setEnabled(false);
    searchBtn_->setEnabled(false);
    statusLabel_->setText(tr("Searching…"));
    QApplication::processEvents();

    // Same regex BlueBrick uses on the directory listing's HTML body.
    // The capture group is the displayed filename ("name.zip" or
    // "name.X.Y.zip"). We trust the source URL is a directory base so
    // sourceUrl = url + filename.
    static const QRegularExpression rx(
        QStringLiteral("<a href=\"[^\"]+\\.zip\">(?<name>[^<]+\\.zip)</a>"),
        QRegularExpression::CaseInsensitiveOption);

    QVector<Package> all;
    QStringList errors;
    for (int i = 0; i < urls.size(); ++i) {
        const QString& url = urls[i];
        statusLabel_->setText(tr("Searching %1/%2: %3").arg(i + 1).arg(urls.size()).arg(url));
        QApplication::processEvents();
        QString err;
        const QString html = fetchUrl(QUrl(url), &err);
        if (html.isEmpty()) {
            errors.append(tr("%1 — %2").arg(url, err.isEmpty() ? tr("empty response") : err));
            continue;
        }
        auto it = rx.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString filename = m.captured(QStringLiteral("name"));
            // Split off ".zip", then split on the first dot to extract a
            // version suffix. e.g. "Cars.1.2.zip" -> name="Cars", ver="1.2".
            QString stem = filename;
            stem.chop(4);  // remove ".zip"
            Package pkg;
            const int dot = stem.indexOf(QChar('.'));
            if (dot > 0) {
                pkg.name = stem.left(dot);
                pkg.version = stem.mid(dot + 1);
            } else {
                pkg.name = stem;
            }
            pkg.fileName  = filename;
            pkg.sourceUrl = url + filename;
            all.append(pkg);
        }
    }

    const auto installed = readInstalledVersions();
    candidates_->clear();
    int fresh = 0, updates = 0, included = 0;
    for (const auto& p : std::as_const(all)) {
        const auto have = installed.constFind(p.name.toLower());
        auto* item = new QListWidgetItem(candidates_);
        item->setData(kUrlRole, p.sourceUrl);
        item->setData(kFileRole, p.fileName);
        item->setData(kNameRole, p.name);
        item->setData(kVersionRole, p.version);
        item->setToolTip(p.sourceUrl);
        if (have != installed.constEnd()
            && parts::PartsLibrary::comparePackageVersions(have.value(), p.version) >= 0) {
            // Already there, this version or newer: shown, not offered.
            item->setText(have.value().isEmpty() || have.value() == p.version
                              ? tr("%1 — Included").arg(p.name)
                              : tr("%1 — Included (you have %2)").arg(p.name, have.value()));
            item->setFlags(item->flags() & ~(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled));
            ++included;
            continue;
        }
        if (have != installed.constEnd()) {
            item->setText(
                tr("%1 — Update %2 → %3")
                    .arg(p.name, have.value().isEmpty() ? tr("(no version)") : have.value(), p.version));
            ++updates;
        } else {
            item->setText(p.version.isEmpty() ? p.name : tr("%1 (version %2)").arg(p.name, p.version));
            ++fresh;
        }
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }

    QString status;
    if (all.isEmpty()) status = tr("No part packs found.");
    else if (fresh + updates == 0) status = tr("You already have all %n part pack(s).", nullptr, included);
    else status = tr("%1 new, %n update(s), %2 already included.", nullptr, updates).arg(fresh).arg(included);
    if (!errors.isEmpty())
        status += QChar(' ') + tr("Couldn’t search:") + QChar(' ') + errors.join(QStringLiteral("; "));
    statusLabel_->setText(status);
    searchBtn_->setEnabled(true);
    downloadBtn_->setEnabled(fresh + updates > 0);
}

bool DownloadCenterDialog::downloadAndInstall(const Package& pkg, QString* error) {
    // Streamed to a temporary file, stopped as soon as it's too big, when it
    // stalls, or on Cancel.
    QTemporaryFile file;
    if (!file.open()) {
        if (error) *error = tr("There's no room for the download on this computer.");
        return false;
    }
    QNetworkRequest req(QUrl(pkg.sourceUrl));
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(stallTimeoutMs_);
    QNetworkReply* rep = nam_->get(req);
    current_ = rep;
    bool tooBig = false, writeFailed = false;
    qint64 received = 0;
    const auto take = [&] {
        const QByteArray chunk = rep->readAll();
        received += chunk.size();
        if (received > maxPackageBytes_) {
            tooBig = true;
            rep->abort();
            return;
        }
        if (file.write(chunk) != chunk.size()) {
            writeFailed = true;
            rep->abort();
        }
    };
    QObject::connect(rep, &QNetworkReply::metaDataChanged, this, [&] {
        const qint64 length = rep->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > maxPackageBytes_) {
            tooBig = true;
            rep->abort();
        }
    });
    QObject::connect(rep, &QNetworkReply::readyRead, this, take);
    QObject::connect(rep, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (total <= 0) return;
        progress_->setMaximum(1000);
        progress_->setValue(static_cast<int>(done * 1000 / total));
    });
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!rep->isFinished()) loop.exec();
    if (!tooBig && !writeFailed && rep->error() == QNetworkReply::NoError) take();
    const QNetworkReply::NetworkError netError = rep->error();
    const QString netMessage = rep->errorString();
    rep->deleteLater();
    current_ = nullptr;
    const qint64 mb = maxPackageBytes_ / (qint64(1024) * 1024);
    if (tooBig) {
        if (error)
            *error = mb > 0 ? tr("It's bigger than %1 MB, so it wasn't downloaded.").arg(mb)
                            : tr("It's too big, so it wasn't downloaded.");
        return false;
    }
    if (writeFailed) {
        if (error) *error = tr("There's no room for the download on this computer.");
        return false;
    }
    if (cancelled_) {
        if (error) *error = tr("Canceled.");
        return false;
    }
    if (netError == QNetworkReply::OperationCanceledError || netError == QNetworkReply::TimeoutError) {
        if (error)
            *error = tr("The download stopped: nothing came for %n second(s).", nullptr,
                        std::max(1, stallTimeoutMs_ / 1000));
        return false;
    }
    if (netError != QNetworkReply::NoError) {
        if (error) *error = netMessage;
        return false;
    }
    if (received == 0) {
        if (error) *error = tr("The download was empty.");
        return false;
    }
    file.seek(0);
    return unpackAndMove(file.readAll(), error);
}

bool DownloadCenterDialog::unpackAndMove(const QByteArray& zipBytes, QString* error) {
    const import::SafeZip zip(zipBytes);
    if (!zip.isValid()) {
        if (error) *error = tr("The download isn't a part pack this app can read.");
        return false;
    }
    if (!QDir().mkpath(libraryRoot_)) {
        if (error) *error = tr("Couldn't make the folder %1.").arg(QDir::toNativeSeparators(libraryRoot_));
        return false;
    }
    // Unpacked beside the library first, so a bad entry leaves nothing
    // half-installed; only a whole package is moved into place.
    QTemporaryDir staging(QDir(libraryRoot_).filePath(QStringLiteral(".download-XXXXXX")));
    if (!staging.isValid()) {
        if (error) *error = tr("Couldn't make the folder %1.").arg(QDir::toNativeSeparators(libraryRoot_));
        return false;
    }
    // Every entry is treated as hostile: package sources are user-
    // configurable URLs. Names can't escape the library root ("../"),
    // symlink entries aren't recreated, and sizes are bounded (SafeZip).
    const QDir stage(staging.path());
    for (const auto& entry : zip.entries()) {
        const QString abs = import::resolveArchiveEntryPath(stage.absolutePath(), entry.name);
        if (abs.isEmpty() || entry.isSymLink) {
            if (error)
                *error =
                    tr("It has a file that would go outside the parts folder (%1), so nothing was installed.")
                        .arg(entry.name);
            return false;
        }
        if (entry.isDir) {
            if (!QDir().mkpath(abs)) {
                if (error) *error = tr("Couldn't make the folder %1.").arg(QDir::toNativeSeparators(abs));
                return false;
            }
            continue;
        }
        const auto entryBytes = zip.read(entry);
        if (!entryBytes) {
            if (error) *error = tr("It's damaged (%1), so nothing was installed.").arg(entry.name);
            return false;
        }
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile out(abs);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(*entryBytes) != entryBytes->size()) {
            if (error)
                *error = tr("There's no room for it in %1.").arg(QDir::toNativeSeparators(libraryRoot_));
            return false;
        }
    }
    // Move each top-level folder or file into the library, keeping the old
    // one until the new one is in place.
    const QFileInfoList top = stage.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    struct Moved {
        QString target, backup;
    };
    QList<Moved> moved;
    const auto rollBack = [&moved] {
        for (const Moved& m : std::as_const(moved)) {
            QFileInfo(m.target).isDir() ? QDir(m.target).removeRecursively() : QFile::remove(m.target);
            if (!m.backup.isEmpty()) QFile::rename(m.backup, m.target);
        }
    };
    for (const QFileInfo& item : top) {
        const QString target = QDir(libraryRoot_).filePath(item.fileName());
        QString backup;
        if (QFileInfo::exists(target)) {
            backup = QDir(staging.path()).filePath(QStringLiteral(".old-") + item.fileName());
            if (!QFile::rename(target, backup)) {
                rollBack();
                if (error)
                    *error =
                        tr("%1 is in use, so it couldn't be replaced.").arg(QDir::toNativeSeparators(target));
                return false;
            }
        }
        if (!QFile::rename(item.absoluteFilePath(), target)) {
            if (!backup.isEmpty()) QFile::rename(backup, target);
            rollBack();
            if (error) *error = tr("Couldn't put it in %1.").arg(QDir::toNativeSeparators(libraryRoot_));
            return false;
        }
        moved.push_back({ target, backup });
    }
    return true; // the old copies go with the staging folder
}

void DownloadCenterDialog::onDownloadClicked() {
    QVector<Package> selected;
    for (int i = 0; i < candidates_->count(); ++i) {
        QListWidgetItem* it = candidates_->item(i);
        if (!(it->flags() & Qt::ItemIsUserCheckable) || it->checkState() != Qt::Checked) continue;
        Package p;
        p.sourceUrl = it->data(kUrlRole).toString();
        p.fileName = it->data(kFileRole).toString();
        p.name = it->data(kNameRole).toString();
        p.version = it->data(kVersionRole).toString();
        selected.append(p);
    }
    if (selected.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
            tr("Tick at least one part pack to install."));
        return;
    }

    downloadBtn_->setEnabled(false);
    searchBtn_->setEnabled(false);
    closeBtn_->setEnabled(false);
    cancelBtn_->setVisible(true);
    progress_->setVisible(true);
    progress_->setMaximum(0);
    downloading_ = true;
    cancelled_ = false;

    QStringList failures;
    int ok = 0;
    for (int i = 0; i < selected.size() && !cancelled_; ++i) {
        const auto& p = selected[i];
        statusLabel_->setText(tr("Downloading %1 (%2 of %3)…").arg(p.name).arg(i + 1).arg(selected.size()));
        progress_->setMaximum(0);
        QString err;
        if (downloadAndInstall(p, &err)) {
            ++ok;
            // Mark the row so the user can see what landed.
            for (int j = 0; j < candidates_->count(); ++j) {
                QListWidgetItem* row = candidates_->item(j);
                if (row->data(kFileRole).toString() != p.fileName) continue;
                row->setText(
                    tr("%1 — Installed")
                        .arg(p.version.isEmpty() ? p.name : tr("%1 (version %2)").arg(p.name, p.version)));
                row->setForeground(QBrush(QColor(40, 130, 60)));
                row->setCheckState(Qt::Unchecked);
                row->setFlags(row->flags() & ~Qt::ItemIsUserCheckable);
                break;
            }
        } else if (!cancelled_) {
            failures.append(QStringLiteral("%1: %2").arg(p.name, err));
        }
    }
    downloading_ = false;
    progress_->setVisible(false);
    cancelBtn_->setVisible(false);
    closeBtn_->setEnabled(true);
    installedCount_ += ok;

    QString status = tr("Installed %n part pack(s).", nullptr, ok);
    if (cancelled_) status += QChar(' ') + tr("The rest were canceled.");
    statusLabel_->setText(status);
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Some part packs couldn't be installed:\n%1").arg(failures.join(QChar('\n'))));
    }
    searchBtn_->setEnabled(true);
    // More can be ticked and installed; the dialog stays open until Close.
    bool more = false;
    for (int j = 0; j < candidates_->count(); ++j)
        more |= bool(candidates_->item(j)->flags() & Qt::ItemIsUserCheckable);
    downloadBtn_->setEnabled(more);
}

}  // namespace bld::ui
