#include "Autosave.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace bld::ui {

namespace {

constexpr const char* kLegacySourceKey = "autosave/sourceFile";

QString newId() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('-')
           + QUuid::createUuid().toString(QUuid::Id128).left(8);
}

QString appData() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString legacyPath() {
    return appData() + QStringLiteral("/autosave.bld-layout");
}

QString fileOf(const QString& id, const char* suffix) {
    return AutosaveSlot::folder() + QLatin1Char('/') + id + QLatin1String(suffix);
}

// A lock only a running copy of the app can hold: one left by a copy that
// stopped is taken over (its process is gone), however old or new it is.
std::unique_ptr<QLockFile> lockFor(const QString& id) {
    auto lock = std::make_unique<QLockFile>(fileOf(id, ".lock"));
    lock->setStaleLockTime(0);
    return lock;
}

} // namespace

QString AutosaveLeftover::displayName() const {
    return sourceFile.isEmpty() ? QCoreApplication::translate("bld::ui::Autosave", "New layout")
                                : QFileInfo(sourceFile).fileName();
}

AutosaveSlot::AutosaveSlot() : id_(newId()) {
}

AutosaveSlot::~AutosaveSlot() = default;

QString AutosaveSlot::folder() {
    return appData() + QStringLiteral("/autosave");
}

QString AutosaveSlot::path() {
    if (!lock_) {
        QDir().mkpath(folder());
        lock_ = lockFor(id_);
        // A slot of our own that someone holds can only be a clash of names:
        // take another.
        for (int tries = 0; tries < 5 && !lock_->tryLock(0); ++tries) {
            id_ = newId();
            lock_ = lockFor(id_);
        }
    }
    return fileOf(id_, ".bld-layout");
}

void AutosaveSlot::noteWritten(const QString& sourceFile) {
    QSaveFile meta(fileOf(id_, ".json"));
    if (!meta.open(QIODevice::WriteOnly)) return;
    meta.write(QJsonDocument(
                   QJsonObject{
                       { QStringLiteral("sourceFile"), sourceFile },
                       { QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
                   })
                   .toJson(QJsonDocument::Compact));
    meta.commit();
}

bool AutosaveSlot::hasFile() const {
    return QFileInfo::exists(fileOf(id_, ".bld-layout"));
}

void AutosaveSlot::removeFile() {
    // Only a slot this window holds is its own to delete.
    if (!lock_) return;
    QFile::remove(fileOf(id_, ".bld-layout"));
    QFile::remove(fileOf(id_, ".json"));
}

void AutosaveSlot::release() {
    removeFile();
    lock_.reset();
    id_ = newId();
}

bool AutosaveSlot::adopt(const AutosaveLeftover& leftover) {
    release();
    if (leftover.legacy()) {
        // The old single autosave moves into a slot of its own.
        const QString target = path();
        QFile::remove(target);
        if (!QFile::rename(leftover.layoutPath, target) && !QFile::copy(leftover.layoutPath, target))
            return false;
        QFile::remove(leftover.layoutPath);
        QSettings().remove(QString::fromLatin1(kLegacySourceKey));
        noteWritten(leftover.sourceFile);
        return true;
    }
    QDir().mkpath(folder());
    auto lock = lockFor(leftover.id);
    if (!lock->tryLock(0)) return false;
    id_ = leftover.id;
    lock_ = std::move(lock);
    return true;
}

QList<AutosaveLeftover> AutosaveSlot::leftovers() {
    QList<AutosaveLeftover> out;
    const QDir dir(folder());
    for (const QFileInfo& info : dir.entryInfoList({ QStringLiteral("*.bld-layout") }, QDir::Files)) {
        const QString id = info.completeBaseName();
        // Held by a running copy of the app (this one included): not left over.
        auto lock = lockFor(id);
        if (!lock->tryLock(0)) continue;
        lock->unlock();
        AutosaveLeftover l{ id, info.absoluteFilePath(), {}, info.lastModified() };
        QFile meta(fileOf(id, ".json"));
        if (meta.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
            l.sourceFile = o.value(QLatin1String("sourceFile")).toString();
            const QDateTime at =
                QDateTime::fromString(o.value(QLatin1String("savedAt")).toString(), Qt::ISODate);
            if (at.isValid()) l.savedAt = at.toLocalTime();
        }
        out << l;
    }
    const QFileInfo legacy(legacyPath());
    if (legacy.exists()) {
        const QString source = QSettings().value(QString::fromLatin1(kLegacySourceKey)).toString();
        // As before: one older than the file it was editing was saved over since.
        const QFileInfo src(source);
        if (source.isEmpty() || !src.exists() || src.lastModified() < legacy.lastModified())
            out << AutosaveLeftover{ QStringLiteral("legacy"), legacy.absoluteFilePath(), source,
                                     legacy.lastModified() };
    }
    std::sort(out.begin(), out.end(),
              [](const AutosaveLeftover& a, const AutosaveLeftover& b) { return a.savedAt > b.savedAt; });
    return out;
}

bool AutosaveSlot::discard(const AutosaveLeftover& leftover) {
    if (leftover.legacy()) {
        QSettings().remove(QString::fromLatin1(kLegacySourceKey));
        return QFile::remove(leftover.layoutPath) || !QFileInfo::exists(leftover.layoutPath);
    }
    // Not one a running copy just took over.
    auto lock = lockFor(leftover.id);
    if (!lock->tryLock(0)) return false;
    QFile::remove(fileOf(leftover.id, ".json"));
    const bool removed = QFile::remove(leftover.layoutPath) || !QFileInfo::exists(leftover.layoutPath);
    lock->unlock();
    return removed;
}

} // namespace bld::ui
