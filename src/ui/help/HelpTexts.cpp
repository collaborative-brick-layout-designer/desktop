#include "HelpTexts.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// help.qrc lives in a static library: register it before reading it.
static void initHelpResources() { Q_INIT_RESOURCE(help); }

namespace bld::ui::help {

namespace {

// Marked with QT_TRANSLATE_NOOP("HelpTexts", ...); helpEntry() translates them.
struct Raw {
    const char* key;
    const char* learnMoreUrl;
    const char* title;
    const char* shortText;
    const char* more;
};

// The shared entries: help-texts.json, the web's file word for word (a Qt
// resource, help.qrc), read once. HelpTextStrings.cpp marks the same
// strings for lupdate; helpEntry() translates them as it hands them out.
struct SharedEntry {
    QString key;
    QByteArray title;
    QByteArray shortText;
    QByteArray more;
    QString learnMoreUrl;
};

const QList<SharedEntry>& sharedEntries() {
    static const QList<SharedEntry> entries = [] {
        QList<SharedEntry> out;
        initHelpResources();
        QFile f(QStringLiteral(":/bld/help/help-texts.json"));
        if (!f.open(QIODevice::ReadOnly)) return out;
        // The order of the keys is the catalogue's order, so read the
        // object's keys as they stand in the file, not sorted.
        const QByteArray bytes = f.readAll();
        const QJsonObject all = QJsonDocument::fromJson(bytes).object().value(QLatin1String("entries")).toObject();
        static const QRegularExpression keyRe(QStringLiteral(R"re(^    "([^"]+)": \{)re"), QRegularExpression::MultilineOption);
        QStringList keys;
        for (auto it = keyRe.globalMatch(QString::fromUtf8(bytes)); it.hasNext();) keys << it.next().captured(1);
        if (keys.size() != all.size()) keys = all.keys();  // laid out some other way: sorted, but complete
        for (const QString& key : keys) {
            const QJsonObject e = all.value(key).toObject();
            out.push_back({ key, e.value(QLatin1String("title")).toString().toUtf8(),
                            e.value(QLatin1String("short")).toString().toUtf8(),
                            e.value(QLatin1String("more")).toString().toUtf8(),
                            e.value(QLatin1String("learnMoreUrl")).toString() });
        }
        return out;
    }();
    return entries;
}

// Controls only the desktop app has.
constexpr Raw kDesktopOnly[] = {
    { "server.send", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Sending to the server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Put a copy of something from this computer on the server."),
      QT_TRANSLATE_NOOP("HelpTexts", "Things marked Not on the server yet are only on this computer. Send… "
                                     "asks whether it’s yours or a club’s, then puts a copy there; the one "
                                     "here stays as it is. A club’s things belong to the club, and you stay "
                                     "credited as the author.") },
    { "catalog.share", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Share to the public catalog"),
      QT_TRANSLATE_NOOP("HelpTexts", "Let anyone on this server copy it into their own layouts."),
      QT_TRANSLATE_NOOP("HelpTexts", "A copy of it as it is now goes to the catalog; your own stays yours. "
                                     "On most servers a moderator looks at it first. Later, Publish this "
                                     "update sends your changes. Its card shows the drawn picture; you can "
                                     "pick a photo of your own on the web.") },
    // File › Open from Server… (the Server window)
    { "server.window", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Open from Server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Everything you and your clubs keep on a server, in one place."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a server at the top, and Show to see only yours or one club’s. "
                                     "Open a layout live to work on it with everyone, or download a copy. "
                                     "Start a layout in a venue, put a module in the layout that’s open, or "
                                     "bring a custom part into your parts. The Catalog and Collections tabs "
                                     "have what people shared for everyone.") },
    // Bringing in models (Tools › Import)
    { "import.clash", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Replace or keep both"),
      QT_TRANSLATE_NOOP("HelpTexts", "You already have a part with this name. Update it, or add this model "
                                     "as a new part."),
      QT_TRANSLATE_NOOP("HelpTexts", "Replace the existing part keeps its number, so every layout that "
                                     "uses it shows the new model straight away. Keep both adds this model "
                                     "with a -2 after its name, and the old part stays as it was. To swap "
                                     "the model of a part later, right-click it in the Parts panel or on the "
                                     "map and pick Replace with Another File.") },
    { "import.moved", "",
      QT_TRANSLATE_NOOP("HelpTexts", "The original file moved"),
      QT_TRANSLATE_NOOP("HelpTexts", "The model this part was made from isn’t where it was. Show the app "
                                     "where it is now."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick the model in its new place, or any other model, and the part is "
                                     "made again from it with the same number, turn and alignment. From then "
                                     "on, Re-import uses the file you picked.") },
    // Preferences (Edit > Preferences...)
    { "prefs.budgetUnlimited", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Parts without a budget"),
      QT_TRANSLATE_NOOP("HelpTexts", "Choose whether parts that have no number in the budget can be used "
                                     "freely."),
      QT_TRANSLATE_NOOP("HelpTexts", "Ticked, a part with no number in the budget can be used as often as "
                                     "you like. Unticked, it can’t be used at all until you give it a "
                                     "number.") },
    // File › Recover Unsaved Work…, and the list shown at start
    { "file.recover", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Recover unsaved work"),
      QT_TRANSLATE_NOOP("HelpTexts", "Changes the app kept for you when it closed before you saved them."),
      QT_TRANSLATE_NOOP("HelpTexts", "While you work, the app keeps a copy of your changes every few seconds. "
                                     "If it closes before you save, they wait here. Recover opens the layout "
                                     "with them, ready to save; Discard deletes them. Saving or closing "
                                     "normally clears them.") },
    // The card after opening a file that couldn't all be read
    { "file.openProblems", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Opened with problems"),
      QT_TRANSLATE_NOOP("HelpTexts", "Part of the file couldn't be read, or was changed by another program."),
      QT_TRANSLATE_NOOP("HelpTexts", "What could be read is open. When you save over the file, the app "
                                     "first keeps a copy of it as it was, ending in .bak, so nothing is lost. "
                                     "Save to a new name to leave the file alone.") },
    { "prefs.newTemplate", "",
      QT_TRANSLATE_NOOP("HelpTexts", "New layout template"),
      QT_TRANSLATE_NOOP("HelpTexts", "A layout that every new layout starts from."),
      QT_TRANSLATE_NOOP("HelpTexts", "Handy if your club always starts with the same baseplates or "
                                     "sheets. Leave it empty to start from a blank map.") },
    { "prefs.connectionPoints", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Connection points"),
      QT_TRANSLATE_NOOP("HelpTexts", "The small dots where track and other pieces join."),
      QT_TRANSLATE_NOOP("HelpTexts", "Normally they show only on the pieces you pick. Tick this to see "
                                     "them on every piece, which helps when joining long runs of track.") },
    { "prefs.partLibraries", "",
      QT_TRANSLATE_NOOP("HelpTexts", "More part folders"),
      QT_TRANSLATE_NOOP("HelpTexts", "Extra folders of parts to show in the Parts panel, like ones a "
                                     "friend shared."),
      QT_TRANSLATE_NOOP("HelpTexts", "Add a folder and its parts appear next to the built-in ones. "
                                     "Removing a folder here doesn’t delete anything.") },
    { "parts.getMore", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Get more parts"),
      QT_TRANSLATE_NOOP("HelpTexts", "Packs of parts for BlueBrick layouts, like trains, roads or Duplo."),
      QT_TRANSLATE_NOOP("HelpTexts", "Packs you already have say Included, and a newer version of one says "
                                     "Update. Installed packs go in your own parts folder and show in the "
                                     "Parts panel at once.") },
    { "prefs.ldraw", "",
      QT_TRANSLATE_NOOP("HelpTexts", "LDraw library"),
      QT_TRANSLATE_NOOP("HelpTexts", "The LDraw parts, needed to bring in models from Studio, LDraw or "
                                     "LEGO Digital Designer."),
      QT_TRANSLATE_NOOP("HelpTexts", "If you don’t have it, press the download button and the app fetches "
                                     "it for you. You only need it for bringing in models.") },
    { "prefs.zoomSpeed", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Zoom speed"),
      QT_TRANSLATE_NOOP("HelpTexts", "How far one turn of the mouse wheel zooms the map."),
      QT_TRANSLATE_NOOP("HelpTexts", "Gentle suits a touchpad or a wheel that spins freely; Fast gets from the "
                                     "whole layout to one part in a few turns.") },
    { "prefs.language", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Language"),
      QT_TRANSLATE_NOOP("HelpTexts", "The language of the app's menus, windows and help."),
      QT_TRANSLATE_NOOP("HelpTexts", "Match my computer uses your computer's language when the app has it, "
                                     "and English otherwise. A language you pick shows the next time the app "
                                     "starts.") },
    // Working with a server
    { "connect.server", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Server address"),
      QT_TRANSLATE_NOOP("HelpTexts", "The address of your club’s layout server, the same one you open in "
                                     "your browser."),
      QT_TRANSLATE_NOOP("HelpTexts", "Ask whoever runs your club’s server for it. You sign in once in "
                                     "your browser, and the app remembers you.") },
    { "servers.list", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Your servers"),
      QT_TRANSLATE_NOOP("HelpTexts", "Every layout server you use: your club’s, a friend’s, and so on."),
      QT_TRANSLATE_NOOP("HelpTexts", "Add a server by the web address you open in your browser, then sign "
                                     "in. Each server remembers your sign-in on its own, and its layouts "
                                     "and parts stay with it.") },
    { "servers.pick", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Choose the server your layout is on. The ones you opened lately are "
                                     "listed under it."),
      QT_TRANSLATE_NOOP("HelpTexts", "Double-click a recent layout to open it straight away, or a server "
                                     "to see all its layouts. Manage Servers adds, renames or removes "
                                     "them.") },
    { "servers.main", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Main server"),
      QT_TRANSLATE_NOOP("HelpTexts", "Your settings follow your account on the Main server."),
      QT_TRANSLATE_NOOP("HelpTexts", "Theme, colors and the other settings are kept with your account "
                                     "there, so they are the same on every computer, whichever server "
                                     "you are working on.") },
    { "publish.owner", "/help#sharing",
      QT_TRANSLATE_NOOP("HelpTexts", "Save to"),
      QT_TRANSLATE_NOOP("HelpTexts", "Who the layout belongs to on the server: you, or your club."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick your club to keep the layout with the club, not with one "
                                     "person. The owner decides who else can see or change it.") },
    { "compare.about", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Your offline changes"),
      QT_TRANSLATE_NOOP("HelpTexts", "What you changed while offline, next to what changed on the server."),
      QT_TRANSLATE_NOOP("HelpTexts", "Pick a row to see it on the map. Changes that don’t clash are "
                                     "simply added; for the ones that clash, choose what to keep.") },
    { "compare.keep", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Keep mine or the server’s"),
      QT_TRANSLATE_NOOP("HelpTexts", "For each clash, choose your version, the server’s, or both."),
      QT_TRANSLATE_NOOP("HelpTexts", "“Keep both” adds yours as a copy next to the server’s. “Later” "
                                     "keeps your changes waiting, so nothing is lost.") },
    { "upload.parts", "",
      QT_TRANSLATE_NOOP("HelpTexts", "Upload your parts"),
      QT_TRANSLATE_NOOP("HelpTexts", "Parts you made yourself that the server doesn’t have yet."),
      QT_TRANSLATE_NOOP("HelpTexts", "Upload them so everyone who opens your layouts on the server sees "
                                     "them too. Pick your club as the owner to share them with the club.") },
};

// The desktop's own words for a few shared keys, where the web's would be
// wrong here. This is the only list of them: HelpTextsTest lets exactly
// these keys differ from the web. A null field keeps the web's words.
struct Override {
    const char* key;
    const char* title;
    const char* shortText;
    const char* more;
};

constexpr Override kDesktopOverrides[] = {
    // The desktop writes the pictures into a folder, not a zip file.
    { "share.exportAllViews", nullptr,
      QT_TRANSLATE_NOOP("HelpTexts", "One picture of every saved view, into a folder you choose."),
      nullptr },
    // The desktop also keeps modules as files, and changes a server module in the window itself.
    { "panel.moduleLibrary", nullptr, nullptr,
      QT_TRANSLATE_NOOP("HelpTexts", "This computer keeps modules as files in a folder. The server's tab has "
                                     "yours and your clubs', shared with everyone in the club, and Catalog has "
                                     "what people shared for everyone. Add to layout drops a copy in; to change "
                                     "a server module, pick ⋯ › Open to change it, then Save makes a new version.") },
};

HelpEntry translated(const Raw& r) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("HelpTexts", s); };
    return { tr(r.title), tr(r.shortText), tr(r.more), QString::fromLatin1(r.learnMoreUrl) };
}

HelpEntry withOverride(HelpEntry e, const Override& o) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("HelpTexts", s); };
    if (o.title) e.title = tr(o.title);
    if (o.shortText) e.shortText = tr(o.shortText);
    if (o.more) e.more = tr(o.more);
    return e;
}

}  // namespace

std::optional<HelpEntry> sharedHelpEntry(const QString& key) {
    const auto tr = [](const QByteArray& s) { return QCoreApplication::translate("HelpTexts", s.constData()); };
    for (const SharedEntry& e : sharedEntries())
        if (e.key == key) return HelpEntry{ tr(e.title), tr(e.shortText), tr(e.more), e.learnMoreUrl };
    return std::nullopt;
}

std::optional<HelpEntry> helpEntry(const QString& key) {
    if (auto shared = sharedHelpEntry(key)) {
        for (const Override& o : kDesktopOverrides)
            if (key == QLatin1String(o.key)) return withOverride(*shared, o);
        return shared;
    }
    for (const Raw& r : kDesktopOnly)
        if (key == QLatin1String(r.key)) return translated(r);
    return std::nullopt;
}

QStringList sharedHelpKeys() {
    QStringList keys;
    for (const SharedEntry& e : sharedEntries()) keys << e.key;
    return keys;
}

QStringList desktopOnlyHelpKeys() {
    QStringList keys;
    for (const Raw& r : kDesktopOnly) keys << QString::fromLatin1(r.key);
    return keys;
}

QStringList desktopOverriddenHelpKeys() {
    QStringList keys;
    for (const Override& o : kDesktopOverrides) keys << QString::fromLatin1(o.key);
    return keys;
}

QStringList helpKeys() { return sharedHelpKeys() + desktopOnlyHelpKeys(); }

}  // namespace bld::ui::help
