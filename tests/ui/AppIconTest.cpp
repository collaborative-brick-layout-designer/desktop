// The app's logo: the window icon holds every size from 16 to 512 px, each
// the PNG rendered at exactly that size (not a big image scaled down), the
// About box shows it, and the packaging carries the same sizes (the Windows
// .ico, the macOS .icns and the Linux hicolor PNGs). The icons are rendered
// from packaging/icons/source/logo.svg by packaging/icons/source/build-icons.sh.

#include "ui/MainWindow.h"
#include "ui/UpdateCheck.h"
#include "ui/theme/AppIcon.h"

#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QtEndian>

using namespace bld;

namespace {

const QString kIcons = QStringLiteral(BLD_SOURCE_DIR "/packaging/icons");

QString pngFor(int px) {
    return px == 256 ? kIcons + QStringLiteral("/brick-layout-designer.png")
                     : kIcons + QStringLiteral("/png/brick-layout-designer-%1.png").arg(px);
}

// Pixmaps keep premultiplied alpha, so compare in that form (the soft
// corner pixels round differently otherwise).
QImage rgba(const QImage& img) { return img.convertToFormat(QImage::Format_RGBA8888_Premultiplied); }

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST(AppIcon, HoldsEverySizeDrawnAtThatSize) {
    const QIcon icon = ui::theme::appIcon();
    ASSERT_FALSE(icon.isNull());
    QList<int> held;
    for (const QSize& s : icon.availableSizes()) {
        EXPECT_EQ(s.width(), s.height());
        held << s.width();
    }
    std::sort(held.begin(), held.end());
    EXPECT_EQ(held, (QList<int>{ 16, 24, 32, 48, 64, 128, 256, 512 }));
    EXPECT_EQ(held, ui::theme::appIconSizes());
    // The title bar's 16 px and the taskbar's 24/32 px are the PNGs drawn
    // at those sizes, pixel for pixel.
    for (int px : ui::theme::appIconSizes()) {
        const QImage want(pngFor(px));
        ASSERT_EQ(want.size(), QSize(px, px)) << pngFor(px).toStdString();
        const QImage got = icon.pixmap(QSize(px, px), 1.0).toImage();
        ASSERT_EQ(got.size(), QSize(px, px)) << px;
        EXPECT_EQ(rgba(got), rgba(want)) << px << " px is not the PNG drawn at that size";
    }
}

TEST(AppIcon, TheAboutBoxShowsTheLogo) {
    QStandardPaths::setTestModeEnabled(true);
    ui::UpdateCheck::setCheckAtStartupEnabled(false);
    parts::PartsLibrary lib;
    ui::MainWindow window(lib);
    EXPECT_EQ(window.windowIcon().availableSizes().size(), ui::theme::appIconSizes().size());

    QAction* about = nullptr;
    for (QAction* a : window.findChildren<QAction*>())
        if (a->text().startsWith(QStringLiteral("&About Collaborative"))) about = a;
    ASSERT_NE(about, nullptr);

    // Modal on Windows and Linux (the timer fires inside its loop), a
    // window of its own on macOS (trigger() returns with it open).
    QImage shown;
    bool asked = false;
    const auto look = [&] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(w);
            if (!box || !box->isVisible()) continue;
            asked = true;
            shown = box->iconPixmap().toImage();
            box->accept();
            return;
        }
    };
    QTimer::singleShot(0, look);
    about->trigger();
    for (int i = 0; i < 100 && !asked; ++i) {
        QTest::qWait(20);
        if (!asked) look();
    }
    ASSERT_TRUE(asked) << "no About box";
    // QMessageBox::about shows the window icon at up to 64 px: the 64 px PNG.
    ASSERT_EQ(shown.size() / shown.devicePixelRatio(), QSize(64, 64));
    if (shown.devicePixelRatio() == 1.0) {
        EXPECT_EQ(rgba(shown), rgba(QImage(pngFor(64))));
    }
}

TEST(AppIcon, PackagingCarriesEverySize) {
    // Windows: the .exe's ICO.
    const QByteArray ico = readAll(kIcons + QStringLiteral("/brick-layout-designer.ico"));
    ASSERT_GE(ico.size(), 6);
    const auto* d = reinterpret_cast<const uchar*>(ico.constData());
    EXPECT_EQ(qFromLittleEndian<quint16>(d + 2), 1);  // an icon, not a cursor
    const int n = qFromLittleEndian<quint16>(d + 4);
    ASSERT_GE(ico.size(), 6 + 16 * n);
    QList<int> icoSizes;
    for (int i = 0; i < n; ++i) icoSizes << (d[6 + i * 16] ? d[6 + i * 16] : 256);
    EXPECT_EQ(icoSizes, (QList<int>{ 16, 24, 32, 48, 64, 128, 256 }));

    // macOS: the .icns, PNG entries from 16 to 1024 px.
    const QByteArray icns = readAll(kIcons + QStringLiteral("/brick-layout-designer.icns"));
    ASSERT_GE(icns.size(), 8);
    EXPECT_EQ(icns.left(4), QByteArray("icns"));
    EXPECT_EQ(qFromBigEndian<quint32>(icns.constData() + 4), static_cast<quint32>(icns.size()));
    QStringList types;
    for (qsizetype at = 8; at + 8 <= icns.size();) {
        const quint32 len = qFromBigEndian<quint32>(icns.constData() + at + 4);
        ASSERT_GE(len, 8u);
        types << QString::fromLatin1(icns.mid(at, 4));
        EXPECT_EQ(icns.mid(at + 9, 3), QByteArray("PNG"));
        at += len;
    }
    EXPECT_EQ(types, (QStringList{ "icp4", "icp5", "ic11", "ic12", "ic07", "ic08", "ic13", "ic09", "ic14", "ic10" }));

    // Linux: a hicolor PNG per size, and the SVG for "scalable".
    for (int px : ui::theme::appIconSizes()) EXPECT_EQ(QImageReader(pngFor(px)).size(), QSize(px, px)) << px;
    EXPECT_TRUE(readAll(kIcons + QStringLiteral("/source/logo.svg")).startsWith("<svg"));
}
