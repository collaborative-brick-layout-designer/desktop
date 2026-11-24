#include "FileBackups.h"

#include "../saveload/SidecarIO.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace bld::ui {

namespace {

constexpr const char* kKeepKey = "files/keepSavedCopies";
constexpr const char* kSourceName = "source.txt";
const QString kStampFormat = QStringLiteral("yyyyMMdd-HHmmss-zzz");

struct BackupWords { Q_DECLARE_TR_FUNCTIONS(bld::ui::FileBackups) };

QString absolute(const QString& file) {
    const QFileInfo info(file);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool isBbm(const QString& file) {
    return file.endsWith(QLatin1String(".bbm"), Qt::CaseInsensitive);
}

QByteArray digest(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
}

// Whole or not at all: through a QSaveFile.
bool copyWhole(const QString& from, const QString& to, QString* error) {
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    QSaveFile out(to);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(1 << 20);
        if (chunk.isEmpty() && in.error() != QFile::NoError) {
            if (error) *error = in.errorString();
            out.cancelWriting();
            return false;
        }
        if (out.write(chunk) != chunk.size()) {
            if (error) *error = out.errorString();
            out.cancelWriting();
            return false;
        }
    }
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

bool keep(const QString& file, bool force, QString* error, const QDateTime& now, bool trimAfter = true) {
    if (!QFileInfo(file).isFile()) return true;
    if (!force && FileBackups::keepCount() <= 0) return true;
    const QString dir = FileBackups::folderFor(file);
    if (!QDir().mkpath(dir)) {
        if (error) *error = BackupWords::tr("The folder for earlier saves couldn't be made: %1").arg(QDir::toNativeSeparators(dir));
        return false;
    }
    // Which file these are copies of, for the list when it isn't open.
    {
        QSaveFile src(dir + QLatin1Char('/') + QLatin1String(kSourceName));
        if (src.open(QIODevice::WriteOnly)) {
            src.write(absolute(file).toUtf8());
            src.commit();
        }
    }
    const QString sidecar = isBbm(file) ? saveload::sidecarPathFor(file) : QString();
    const bool hasSidecar = !sidecar.isEmpty() && QFileInfo(sidecar).isFile();
    // The newest copy already holds this: nothing new to keep.
    const QList<SavedCopy> copies = FileBackups::copiesOf(file);
    if (!copies.isEmpty()) {
        const SavedCopy& newest = copies.first();
        const bool sameMap = QFileInfo(newest.path).size() == QFileInfo(file).size() && digest(newest.path) == digest(file);
        const bool sameSidecar = hasSidecar ? (!newest.sidecar.isEmpty() && digest(newest.sidecar) == digest(sidecar))
                                            : newest.sidecar.isEmpty();
        if (sameMap && sameSidecar) return true;
    }
    QString stamp = now.toString(kStampFormat);
    const QString suffix = QFileInfo(file).suffix().toLower();
    QString target = dir + QLatin1Char('/') + stamp + QLatin1Char('.') + suffix;
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = dir + QLatin1Char('/') + stamp + QStringLiteral("-%1.").arg(n) + suffix;
    if (!copyWhole(file, target, error)) return false;
    if (hasSidecar) {
        QString sideError;
        if (!copyWhole(sidecar, saveload::sidecarPathFor(target), &sideError)) {
            QFile::remove(target);
            if (error) *error = sideError;
            return false;
        }
    }
    if (trimAfter) FileBackups::trim(file, std::max(force ? 1 : 0, FileBackups::keepCount()));
    return true;
}

} // namespace

QString FileBackups::folder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/file-backups");
}

QString FileBackups::folderFor(const QString& file) {
    const QByteArray key = QCryptographicHash::hash(absolute(file).toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return folder() + QLatin1Char('/') + QString::fromLatin1(key);
}

int FileBackups::keepCount() {
    return std::clamp(QSettings().value(QLatin1String(kKeepKey), kDefaultKeep).toInt(), 0, kMaxKeep);
}

void FileBackups::setKeepCount(int count) {
    QSettings().setValue(QLatin1String(kKeepKey), std::clamp(count, 0, kMaxKeep));
}

bool FileBackups::keepBeforeSaving(const QString& file, QString* error, const QDateTime& now) {
    return keep(file, false, error, now);
}

QList<SavedCopy> FileBackups::copiesOf(const QString& file) {
    QList<SavedCopy> out;
    const QDir dir(folderFor(file));
    if (!dir.exists()) return out;
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString name = info.fileName();
        if (name == QLatin1String(kSourceName) || name.endsWith(QLatin1String(".bld"), Qt::CaseInsensitive)) continue;
        if (name.endsWith(QLatin1String(".tmp"))) continue;
        const QDateTime at = QDateTime::fromString(name.left(kStampFormat.size()), kStampFormat);
        if (!at.isValid()) continue;
        SavedCopy c{ info.absoluteFilePath(), {}, at, info.size() };
        if (isBbm(name)) {
            const QString side = saveload::sidecarPathFor(c.path);
            if (QFileInfo(side).isFile()) c.sidecar = side;
        }
        out << c;
    }
    std::stable_sort(out.begin(), out.end(), [](const SavedCopy& a, const SavedCopy& b) { return a.at > b.at; });
    return out;
}

QList<BackedUpFile> FileBackups::files() {
    QList<BackedUpFile> out;
    const QDir root(folder());
    for (const QFileInfo& d : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile src(d.absoluteFilePath() + QLatin1Char('/') + QLatin1String(kSourceName));
        if (!src.open(QIODevice::ReadOnly)) continue;
        const QString file = QString::fromUtf8(src.readAll()).trimmed();
        if (file.isEmpty()) continue;
        const QList<SavedCopy> copies = copiesOf(file);
        if (copies.isEmpty()) continue;
        out << BackedUpFile{ file, static_cast<int>(copies.size()), copies.first().at };
    }
    std::sort(out.begin(), out.end(), [](const BackedUpFile& a, const BackedUpFile& b) { return a.newest > b.newest; });
    return out;
}

void FileBackups::trim(const QString& file, int keepCopies) {
    const QList<SavedCopy> copies = copiesOf(file);
    for (qsizetype i = std::max(0, keepCopies); i < copies.size(); ++i) {
        QFile::remove(copies.at(i).path);
        if (!copies.at(i).sidecar.isEmpty()) QFile::remove(copies.at(i).sidecar);
    }
}

bool FileBackups::restore(const SavedCopy& copy, const QString& file, QString* error) {
    if (!QFileInfo(copy.path).isFile()) {
        if (error) *error = BackupWords::tr("That earlier save isn't there any more.");
        return false;
    }
    // The file as it is now, first, even with keeping turned off: a restore can be undone.
    // (Trimmed only after, so the copy being put back can't be the one that goes.)
    if (!keep(file, true, error, QDateTime::currentDateTime(), false)) return false;
    bool ok = copyWhole(copy.path, file, error);
    if (ok && isBbm(file)) {
        const QString sidecar = saveload::sidecarPathFor(file);
        if (!copy.sidecar.isEmpty()) ok = copyWhole(copy.sidecar, sidecar, error);
        else if (QFileInfo::exists(sidecar)) QFile::remove(sidecar);
    }
    trim(file, std::max(1, keepCount()));
    return ok;
}

} // namespace bld::ui
