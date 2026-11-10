#include "Version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QStringList>
#include <QVector>

namespace bld::core {

namespace {

struct Parsed {
    QVector<qlonglong> nums;
    QString pre;
};

std::optional<Parsed> parse(const QString& v) {
    static const QRegularExpression re(
        QStringLiteral("^[vV]?(\\d+(?:\\.\\d+)*)(?:-([0-9A-Za-z.-]+))?(?:\\+.*)?$"));
    const auto m = re.match(v.trimmed());
    if (!m.hasMatch()) return std::nullopt;
    Parsed p;
    for (const QString& n : m.captured(1).split(QLatin1Char('.'))) p.nums << n.toLongLong();
    p.pre = m.captured(2);
    return p;
}

}  // namespace

std::optional<int> compareVersions(const QString& a, const QString& b) {
    const auto pa = parse(a), pb = parse(b);
    if (!pa || !pb) return std::nullopt;
    const qsizetype n = std::max(pa->nums.size(), pb->nums.size());
    for (qsizetype i = 0; i < n; ++i) {
        const qlonglong x = i < pa->nums.size() ? pa->nums[i] : 0;
        const qlonglong y = i < pb->nums.size() ? pb->nums[i] : 0;
        if (x != y) return x > y ? 1 : -1;
    }
    if (pa->pre.isEmpty() != pb->pre.isEmpty()) return pa->pre.isEmpty() ? 1 : -1;
    return 0;
}

QString desktopDownloadUrl() {
    return QStringLiteral("https://github.com/collaborative-brick-layout-designer/desktop/releases/latest");
}

namespace {
QString& channelStorage() {
    static QString channel;
    return channel;
}
}  // namespace

QString buildChannel() { return channelStorage(); }
void setBuildChannel(const QString& channel) { channelStorage() = channel.trimmed(); }

QString thirdPartyNoticesUrl() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString& rel : {
             QStringLiteral("/THIRD-PARTY-NOTICES.txt"),
             QStringLiteral("/../Resources/THIRD-PARTY-NOTICES.txt"),
             QStringLiteral("/../share/doc/BrickLayoutDesigner/THIRD-PARTY-NOTICES.txt") }) {
        const QFileInfo f(exeDir + rel);
        if (f.isFile()) return QUrl::fromLocalFile(QDir::cleanPath(f.absoluteFilePath())).toString();
    }
    return QStringLiteral(
        "https://github.com/collaborative-brick-layout-designer/desktop/blob/main/THIRD-PARTY-NOTICES.txt");
}

}  // namespace bld::core
