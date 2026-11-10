// The words the user sees agree with the web app. Every tr("…") string in
// src/ is scanned:
//   - a map is made of "sheets", never "layers" (the file format and the
//     code still say Layer; the screen does not);
//   - "Venue library" / "Module library" use the web's casing.

#include <gtest/gtest.h>

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct Hit {
    QString file;
    QString text;
};

// Every literal passed to tr(...) or QObject::tr(...) in the C++ sources.
QList<Hit> trStrings() {
    QList<Hit> out;
    static const QRegularExpression re(QStringLiteral(R"re(\btr\(\s*"((?:[^"\\]|\\.)*)")re"));
    QDirIterator it(QStringLiteral(BLD_SOURCE_DIR "/src"), { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(f.readAll());
        for (auto m = re.globalMatch(text); m.hasNext();) {
            out.push_back({ path.mid(QStringLiteral(BLD_SOURCE_DIR).size() + 1), m.next().captured(1) });
        }
    }
    return out;
}

QString describe(const QList<Hit>& hits) {
    QStringList lines;
    for (const Hit& h : hits) lines << h.file + QStringLiteral(": \"") + h.text + QLatin1Char('"');
    return lines.join(QLatin1Char('\n'));
}

} // namespace

TEST(UiWordingTest, FindsTheStrings) {
    // Guard against the scan silently finding nothing.
    EXPECT_GT(trStrings().size(), 500);
}

TEST(UiWordingTest, SaysSheetNotLayer) {
    static const QRegularExpression layer(QStringLiteral(R"(\blayers?\b)"),
                                          QRegularExpression::CaseInsensitiveOption);
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (layer.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say \"sheet\", as the web does:\n" << describe(bad).toStdString();
}

// Menu items and buttons that open a window end in one "…" character, never
// three dots: the two were mixed ("Open..." next to "Share Picture…").
TEST(UiWordingTest, EllipsisIsOneCharacter) {
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (h.text.contains(QLatin1String("..."))) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Use \"…\", not \"...\":\n" << describe(bad).toStdString();
}

// One vocabulary in both apps (docs/glossary.md): "turn", "club" and "web
// address", never "rotate", "org" or "slug" (and "sheet", above). A domain
// such as library.ldraw.org isn't the word "org".
TEST(UiWordingTest, SaysTurnClubAndWebAddress) {
    static const QRegularExpression banned(QStringLiteral(R"((?<![.\w])(rotat\w*|orgs?|slugs?|packages?)\b)"),
                                           QRegularExpression::CaseInsensitiveOption);
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (banned.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "See docs/glossary.md:\n" << describe(bad).toStdString();
}

// The glossary is the same file in both repos.
TEST(UiWordingTest, TheGlossaryIsThere) {
    QFile f(QStringLiteral(BLD_SOURCE_DIR "/docs/glossary.md"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    for (const char* word : { "Sheet", "Turn left / Turn right / Turn 90°", "Send to back / Bring to front", "Club", "Web address" }) {
        EXPECT_TRUE(text.contains(QString::fromUtf8(word))) << word;
    }
    QFile web(QStringLiteral(BLD_SOURCE_DIR "/../collaborative-layout-designer-web/docs/glossary.md"));
    if (web.open(QIODevice::ReadOnly)) {
        EXPECT_EQ(QString::fromUtf8(web.readAll()), text) << "keep docs/glossary.md the same in both repos";
    }
}

TEST(UiWordingTest, LibraryCasingMatchesTheWeb) {
    static const QRegularExpression wrong(QStringLiteral(R"(\b(Venue|Module) &?Library\b)"));
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (wrong.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say \"Venue library\" / \"Module library\":\n" << describe(bad).toStdString();
}

// "Library" alone is ambiguous now that there are parts folders, a Module
// library and a Venue library: every "library" names which one it is.
TEST(UiWordingTest, EveryLibrarySaysWhichOne) {
    static const QRegularExpression named(
        QStringLiteral(R"re(\b(Module|Venue|LDraw|Studio|parts?) &?librar(y|ies)\b|library\.ldraw\.org(/library)?)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression library(QStringLiteral(R"(\blibrar(y|ies)\b)"),
                                            QRegularExpression::CaseInsensitiveOption);
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        QString rest = h.text;
        rest.remove(named);
        if (library.match(rest).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << "Say which library (Module library, Venue library, parts folders…):\n"
                               << describe(bad).toStdString();
}

// The jargon BlueBrick used for modules and budgets is gone.
TEST(UiWordingTest, ModuleAndBudgetWordsArePlain) {
    static const QRegularExpression jargon(QStringLiteral(R"(Flatten|Clone [Mm]odule|Select Members|Budget &?Limitation|Library Part)"));
    QList<Hit> bad;
    for (const Hit& h : trStrings()) {
        if (jargon.match(h.text).hasMatch()) bad.push_back(h);
    }
    EXPECT_TRUE(bad.isEmpty()) << describe(bad).toStdString();
}

// US spelling: "color", not "colour" (Aaron, 2026-10-08), in every source
// file: words, comments and names. LDraw's own `!COLOUR` command is the one
// exception.
TEST(UiWordingTest, SaysColorNotColour) {
    QStringList found;
    for (const char* dir : { "/src", "/tests" }) {
        QDirIterator it(QStringLiteral(BLD_SOURCE_DIR) + QLatin1String(dir),
                        { QStringLiteral("*.cpp"), QStringLiteral("*.h") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String("UiWordingTest.cpp"))) continue;  // this test names it
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            int n = 0;
            for (const QByteArray& raw : f.readAll().split('\n')) {
                ++n;
                QString line = QString::fromUtf8(raw);
                line.remove(QStringLiteral("!COLOUR"));
                if (line.contains(QStringLiteral("colour"), Qt::CaseInsensitive))
                    found << QStringLiteral("%1:%2").arg(path.mid(QStringLiteral(BLD_SOURCE_DIR).size() + 1)).arg(n);
            }
        }
    }
    EXPECT_TRUE(found.isEmpty()) << found.join(QStringLiteral(", ")).toStdString();
}

// US spelling in what people read (Aaron, 2026-10-08): "center", "canceled",
// "meters" and the rest, in every string marked for translation and in the
// help and tour words shared with the web.
TEST(UiWordingTest, SaysUsSpelling) {
    static const QRegularExpression british(
        QStringLiteral(R"(\b(centr(e|es|ed)|cancell(ed|ing)|metres?|catalogue|favourite|organis\w*|behaviour|grey|licence)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression marked(QStringLiteral(R"(\b(tr|QT_TR_NOOP|QT_TRANSLATE_NOOP)\()"));
    static const QRegularExpression literal(QStringLiteral(R"re("((?:[^"\\]|\\.)*)")re"));
    QStringList found;
    QDirIterator it(QStringLiteral(BLD_SOURCE_DIR "/src"), { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (int n = 0; n < lines.size(); ++n) {
            // A marked string, or the next part of one split over lines.
            const bool continues = n > 0 && marked.match(lines[n - 1]).hasMatch()
                                   && lines[n].trimmed().startsWith(QLatin1Char('"'));
            if (!marked.match(lines[n]).hasMatch() && !continues) continue;
            for (auto m = literal.globalMatch(lines[n]); m.hasNext();) {
                const QString text = m.next().captured(1);
                if (british.match(text).hasMatch())
                    found << QStringLiteral("%1:%2 \"%3\"")
                                 .arg(path.mid(QStringLiteral(BLD_SOURCE_DIR).size() + 1))
                                 .arg(n + 1)
                                 .arg(text);
            }
        }
    }
    for (const char* shared : { "/src/ui/help/help-texts.json", "/src/ui/tours/tours.json" }) {
        QFile f(QStringLiteral(BLD_SOURCE_DIR) + QLatin1String(shared));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << shared;
        const QString text = QString::fromUtf8(f.readAll());
        for (auto m = british.globalMatch(text); m.hasNext();) found << QLatin1String(shared) + QStringLiteral(": ") + m.next().captured(0);
    }
    EXPECT_TRUE(found.isEmpty()) << found.join(QLatin1Char('\n')).toStdString();
}

// A drop-down's items are words people read: tr() them, not
// QStringLiteral (they stayed English in every language).
TEST(UiWordingTest, DropDownItemsAreTranslated) {
    static const QRegularExpression plain(QStringLiteral(R"re(addItems?\(\s*(?:\{\s*)?QStringLiteral\("([^"]*[A-Za-z][^"]*)")re"));
    QStringList found;
    QDirIterator it(QStringLiteral(BLD_SOURCE_DIR "/src"), { QStringLiteral("*.cpp") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(f.readAll());
        for (auto m = plain.globalMatch(text); m.hasNext();)
            found << path.mid(QStringLiteral(BLD_SOURCE_DIR).size() + 1) + QStringLiteral(": ") + m.next().captured(1);
    }
    EXPECT_TRUE(found.isEmpty()) << found.join(QLatin1Char('\n')).toStdString();
}
