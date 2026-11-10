// The translations in translations/*.ts:
//   - English's catalog gives every "%n part(s)" its singular and plural;
//   - the language picker's coverage (TranslationCoverage.h) is up to date;
//   - a translation keeps the source's placeholders (%1, %n) and its Alt
//     letter;
//   - each language says "sheet" with its glossary word, never "layer"
//     (docs/glossary.md: "Sheet, not layer");
//   - no context holds strings the app can't find (a tr() inside a lambda
//     or a free function lands in the namespace's name);
//   - the Parts panel's "All categories" isn't cut short in a language.
// The CI's check-translations step makes sure lupdate finds nothing new.

#include "ui/TranslationCoverage.h"
#include "ui/PartsBrowser.h"
#include "parts/PartsLibrary.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QTranslator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace {

struct Message {
    QString context, source;
    QStringList forms;  // the translation, or its plural forms
    bool finished = false;
    bool numerus = false;
};

QList<Message> readTs(const QString& code) {
    QList<Message> out;
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/translations/bld_%1.ts").arg(code));
    if (!f.open(QIODevice::ReadOnly)) return out;
    QXmlStreamReader x(&f);
    QString context;
    Message m;
    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement()) continue;
        const auto name = x.name();
        if (name == QLatin1String("name")) {
            context = x.readElementText();
        } else if (name == QLatin1String("message")) {
            m = Message{};
            m.context = context;
            m.numerus = x.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
        } else if (name == QLatin1String("source")) {
            m.source = x.readElementText();
        } else if (name == QLatin1String("translation")) {
            const auto type = x.attributes().value(QLatin1String("type"));
            m.finished = type.isEmpty();
            if (m.numerus) {
                while (x.readNextStartElement())
                    if (x.name() == QLatin1String("numerusform")) m.forms << x.readElementText();
                    else x.skipCurrentElement();
            } else {
                m.forms << x.readElementText();
            }
            if (m.finished)
                for (const QString& s : std::as_const(m.forms))
                    if (s.trimmed().isEmpty()) m.finished = false;
            out << m;
        }
    }
    return out;
}

const QStringList kLanguages{ QStringLiteral("de"), QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("it"),
                              QStringLiteral("ja"), QStringLiteral("nl"), QStringLiteral("no"), QStringLiteral("pt"),
                              QStringLiteral("sv"), QStringLiteral("zh") };

QSet<QString> placeholders(const QString& s) {
    QSet<QString> out;
    static const QRegularExpression re(QStringLiteral("%(\\d|n)"));
    for (auto m = re.globalMatch(s); m.hasNext();) out.insert(m.next().captured(0));
    return out;
}

// The letter after a single '&' ("&&" is a plain '&').
bool hasMnemonic(const QString& text) {
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != QLatin1Char('&')) continue;
        if (text[i + 1] == QLatin1Char('&')) { ++i; continue; }
        if (text[i + 1].isLetterOrNumber()) return true;
    }
    return false;
}

}  // namespace

TEST(Translations, EnglishSaysOnePartAndThreeParts) {
    const QList<Message> en = readTs(QStringLiteral("en"));
    ASSERT_GT(en.size(), 30);
    for (const Message& m : en) {
        EXPECT_TRUE(m.numerus) << m.source.toStdString();
        ASSERT_EQ(m.forms.size(), 2) << m.source.toStdString();
        for (const QString& f : m.forms) EXPECT_FALSE(f.contains(QStringLiteral("(s)"))) << f.toStdString();
        EXPECT_TRUE(m.finished) << "run scripts/fill-english-plurals.py: " << m.source.toStdString();
    }
}

// What the app loads (main.cpp): "1 part", "3 parts", not "1 part(s)".
TEST(Translations, TheEnglishCatalogPicksTheForm) {
    QTranslator en;
    if (!en.load(QStringLiteral("bld_en"), QStringLiteral(BLD_BINARY_DIR)))
        GTEST_SKIP() << "bld_en.qm isn't built (no Qt Linguist tools)";
    EXPECT_EQ(en.translate("bld::ui::MainWindow", "Printed %n page(s)", nullptr, 1), QStringLiteral("Printed %n page"));
    EXPECT_EQ(en.translate("bld::ui::MainWindow", "Printed %n page(s)", nullptr, 3), QStringLiteral("Printed %n pages"));
    QCoreApplication::installTranslator(&en);
    EXPECT_EQ(QCoreApplication::translate("bld::ui::MainWindow", "Printed %n page(s)", nullptr, 1),
              QStringLiteral("Printed 1 page"));
    EXPECT_EQ(QCoreApplication::translate("bld::ui::MainWindow", "Printed %n page(s)", nullptr, 4),
              QStringLiteral("Printed 4 pages"));
    QCoreApplication::removeTranslator(&en);
}

TEST(Translations, ThePickersCoverageIsUpToDate) {
    for (const QString& code : kLanguages) {
        const QList<Message> all = readTs(code);
        ASSERT_FALSE(all.isEmpty()) << code.toStdString();
        const auto done = std::count_if(all.cbegin(), all.cend(), [](const Message& m) { return m.finished; });
        EXPECT_EQ(bld::ui::translationPercent(code), static_cast<int>(done * 100 / all.size()))
            << code.toStdString() << ": run scripts/translation-coverage.py";
    }
}

TEST(Translations, KeepThePlaceholdersAndTheAltLetter) {
    QStringList bad;
    for (const QString& code : kLanguages)
        for (const Message& m : readTs(code)) {
            if (!m.finished) continue;
            const QSet<QString> want = placeholders(m.source);
            for (const QString& f : m.forms) {
                QSet<QString> got = placeholders(f);
                // A singular may say "one" instead of %n.
                if (m.numerus) got.insert(QStringLiteral("%n"));
                QSet<QString> needed = want;
                if (m.numerus) needed.insert(QStringLiteral("%n"));
                if (got != needed)
                    bad << QStringLiteral("%1 [%2]: \"%3\" -> \"%4\"").arg(code, m.context, m.source, f);
                if (hasMnemonic(m.source) && !hasMnemonic(f))
                    bad << QStringLiteral("%1 [%2]: no Alt letter: \"%3\" -> \"%4\"").arg(code, m.context, m.source, f);
            }
        }
    EXPECT_TRUE(bad.isEmpty()) << bad.mid(0, 40).join(QLatin1Char('\n')).toStdString() << "\n(" << bad.size()
                               << " in all)";
}

TEST(Translations, SheetIsNeverLayer) {
    // The word each language must not use for a sheet (docs/glossary.md).
    const QHash<QString, QRegularExpression> layer{
        { QStringLiteral("de"), QRegularExpression(QStringLiteral("Ebene"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("es"), QRegularExpression(QStringLiteral("\\bcapas?\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("fr"), QRegularExpression(QStringLiteral("calque|\\bcouches?\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("it"), QRegularExpression(QStringLiteral("\\blivell[oi]\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("ja"), QRegularExpression(QStringLiteral("レイヤ")) },
        { QStringLiteral("nl"), QRegularExpression(QStringLiteral("\\blag(en)?\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("no"), QRegularExpression(QStringLiteral("\\blag(et|ene)?\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("pt"), QRegularExpression(QStringLiteral("\\bcamadas?\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("sv"), QRegularExpression(QStringLiteral("\\blag(er|ret|ren)\\b"), QRegularExpression::CaseInsensitiveOption) },
        { QStringLiteral("zh"), QRegularExpression(QStringLiteral("图层")) },
    };
    static const QRegularExpression sheet(QStringLiteral("\\bsheets?\\b|\\barea\\b"), QRegularExpression::CaseInsensitiveOption);
    QStringList bad;
    for (const QString& code : kLanguages)
        for (const Message& m : readTs(code)) {
            if (!m.finished || !sheet.match(m.source).hasMatch()) continue;
            for (const QString& f : m.forms)
                if (layer.value(code).match(f).hasMatch())
                    bad << QStringLiteral("%1: \"%2\" -> \"%3\"").arg(code, m.source, f);
        }
    EXPECT_TRUE(bad.isEmpty()) << bad.join(QLatin1Char('\n')).toStdString();
}

TEST(Translations, EveryContextIsOneTheAppLooksIn) {
    // A namespace's name as a context: tr() called where lupdate can't see a
    // class (a lambda named tr, a free function), so the app never finds it.
    static const QRegularExpression bareNamespace(QStringLiteral("^bld(::[a-z]+)*$"));
    QSet<QString> bad;
    for (const Message& m : readTs(QStringLiteral("de")))
        // (bld::sync::merge is LayoutMerge.cpp's own context, named that.)
        if (bareNamespace.match(m.context).hasMatch() && m.context != QLatin1String("bld::sync::merge"))
            bad.insert(m.context);
    EXPECT_TRUE(bad.isEmpty()) << QStringList(bad.cbegin(), bad.cend()).join(QStringLiteral(", ")).toStdString();
}

// "Toutes les catégories" was cut to "Toutes les catég": the box sized itself
// before its items arrived.
TEST(Translations, ThePartsPanelSaysAllCategoriesInFull) {
    for (const char* code : {"de", "fr", "es"}) {
        QTranslator tr;
        if (!tr.load(QStringLiteral("bld_") + QLatin1String(code), QStringLiteral(BLD_BINARY_DIR)))
            GTEST_SKIP() << "the .qm files aren't built (no Qt Linguist tools)";
        QCoreApplication::installTranslator(&tr);
        {
            bld::parts::PartsLibrary lib;
            bld::ui::PartsBrowser browser(lib);
            browser.resize(300, 600);
            browser.show();
            QApplication::processEvents();
            auto* box = browser.findChild<QComboBox*>(QStringLiteral("partsCategory"));
            ASSERT_NE(box, nullptr);
            const QString all = QCoreApplication::translate("bld::ui::PartsBrowser", "All categories");
            EXPECT_NE(all, QStringLiteral("All categories")) << code;
            EXPECT_GT(box->width(), box->fontMetrics().horizontalAdvance(all)) << code;
        }
        QCoreApplication::removeTranslator(&tr);
    }
}
