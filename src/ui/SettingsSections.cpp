#include "SettingsSections.h"

#include "BudgetSession.h"
#include "ImportPipeline.h"
#include "help/HelpButton.h"

#include "../import/ArchivePath.h"
#include "../import/zip/SafeZip.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>
#include <vector>

namespace bld::ui::settings {

namespace {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(bld::ui::SettingsDialog) };

QFormLayout* form(QVBoxLayout* into) {
    auto* f = new QFormLayout;
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    into->addLayout(f);
    return f;
}

QLabel* muted(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("Muted"));
    l->setWordWrap(true);
    return l;
}

// A path field with a Browse… button; saves to `key` when edited or picked.
QWidget* pathPicker(QWidget* parent, const QString& key, const QString& title, bool folder,
                    const QString& filter, QLineEdit** editOut, const std::function<void()>& saved = {}) {
    auto* wrap = new QWidget(parent);
    wrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* row = new QHBoxLayout(wrap);
    row->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit(QSettings().value(key).toString(), wrap);
    edit->setObjectName(key);
    edit->setAccessibleName(title);
    auto* btn = new QPushButton(Text::tr("Browse…"), wrap);
    btn->setAutoDefault(false);
    btn->setAccessibleName(Text::tr("Choose: %1").arg(title));
    row->addWidget(edit, 1);
    row->addWidget(btn);
    const auto save = [edit, key, saved] {
        QSettings().setValue(key, edit->text().trimmed());
        if (saved) saved();
    };
    QObject::connect(edit, &QLineEdit::editingFinished, edit, save);
    QObject::connect(btn, &QPushButton::clicked, wrap, [edit, wrap, title, folder, filter, save] {
        const QString p = folder ? QFileDialog::getExistingDirectory(wrap, title, edit->text())
                                 : QFileDialog::getOpenFileName(wrap, title, edit->text(), filter);
        if (p.isEmpty()) return;
        edit->setText(p);
        save();
    });
    if (editOut) *editOut = edit;
    return wrap;
}

QCheckBox* checkbox(QWidget* parent, const QString& text, const QString& key, bool fallback,
                    const std::function<void()>& saved = {}) {
    auto* c = new QCheckBox(text, parent);
    c->setObjectName(key);
    c->setChecked(QSettings().value(key, fallback).toBool());
    QObject::connect(c, &QCheckBox::toggled, c, [key, saved](bool on) {
        QSettings().setValue(key, on);
        if (saved) saved();
    });
    return c;
}

void selectData(QComboBox* combo, double value) {
    for (int i = 0; i < combo->count(); ++i)
        if (qFuzzyCompare(combo->itemData(i).toDouble() + 1.0, value + 1.0)) combo->setCurrentIndex(i);
}

}  // namespace

QList<Language> languages() {
    // Each in its own words, so anyone can find theirs.
    return {
        { QString(), Text::tr("Match my computer") },
        { QStringLiteral("en"), QStringLiteral("English") },
        { QStringLiteral("fr"), QStringLiteral("Français") },
        { QStringLiteral("de"), QStringLiteral("Deutsch") },
        { QStringLiteral("nl"), QStringLiteral("Nederlands") },
        { QStringLiteral("pt"), QStringLiteral("Português") },
        { QStringLiteral("es"), QStringLiteral("Español") },
        { QStringLiteral("it"), QStringLiteral("Italiano") },
        { QStringLiteral("no"), QStringLiteral("Norsk") },
        { QStringLiteral("sv"), QStringLiteral("Svenska") },
        { QStringLiteral("zh"), QStringLiteral("中文") },
        { QStringLiteral("ja"), QStringLiteral("日本語") },
    };
}

void addEditing(QVBoxLayout* into, QWidget* parent, const Changed& changed) {
    QFormLayout* f = form(into);

    auto* snap = new QComboBox(parent);
    snap->setObjectName(QStringLiteral("editing/snapStepStuds"));
    snap->addItem(Text::tr("Off"), 0.0);
    for (double d : { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5 })
        snap->addItem(d == 1.0 ? Text::tr("1 stud") : Text::tr("%1 studs").arg(d), d);
    selectData(snap, QSettings().value(QStringLiteral("editing/snapStepStuds"), 0.0).toDouble());
    QObject::connect(snap, &QComboBox::currentIndexChanged, snap, [snap, changed] {
        QSettings().setValue(QStringLiteral("editing/snapStepStuds"), snap->currentData().toDouble());
        if (changed) changed(Change::SnapStep);
    });
    f->addRow(Text::tr("Grid snap:"), help::withHelp(snap, QStringLiteral("toolbar.snap"), parent));

    auto* turn = new QComboBox(parent);
    turn->setObjectName(QStringLiteral("editing/rotationStepDegrees"));
    for (double d : { 90.0, 45.0, 22.5, 11.25, 5.0, 1.0 }) turn->addItem(QString::number(d) + QStringLiteral("°"), d);
    selectData(turn, QSettings().value(QStringLiteral("editing/rotationStepDegrees"), 90.0).toDouble());
    QObject::connect(turn, &QComboBox::currentIndexChanged, turn, [turn, changed] {
        QSettings().setValue(QStringLiteral("editing/rotationStepDegrees"), turn->currentData().toDouble());
        if (changed) changed(Change::TurnStep);
    });
    f->addRow(Text::tr("Turn step:"), help::withHelp(turn, QStringLiteral("toolbar.rotateStep"), parent));

    // The color lives on the button (its lambdas run long after this returns).
    auto* color = new QPushButton(parent);
    color->setObjectName(QStringLiteral("paintColorButton"));
    color->setAutoDefault(false);
    color->setAccessibleName(Text::tr("Paint color"));
    const auto show = [color] {
        const QColor c = color->property("paintColor").value<QColor>();
        QPixmap pm(24, 16);
        pm.fill(c);
        color->setIcon(QIcon(pm));
        color->setText(c.name(QColor::HexArgb));
    };
    color->setProperty("paintColor",
                       QColor(QSettings().value(QStringLiteral("editing/paintColor"), QColor(0, 128, 0).name()).toString()));
    show();
    QObject::connect(color, &QPushButton::clicked, color, [color, show, parent, changed] {
        const QColor c = QColorDialog::getColor(color->property("paintColor").value<QColor>(), parent,
                                                Text::tr("Paint color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        color->setProperty("paintColor", c);
        show();
        QSettings().setValue(QStringLiteral("editing/paintColor"), c.name(QColor::HexArgb));
        if (changed) changed(Change::PaintColor);
    });
    f->addRow(Text::tr("Paint color:"), help::withHelp(color, QStringLiteral("toolbar.paintColor"), parent));

    auto* zoom = new QComboBox(parent);
    zoom->setObjectName(QStringLiteral("general/wheelZoomFactor"));
    zoom->addItem(Text::tr("Gentle"), kZoomGentle);
    zoom->addItem(Text::tr("Normal"), kZoomNormal);
    zoom->addItem(Text::tr("Fast"), kZoomFast);
    selectData(zoom, QSettings().value(QStringLiteral("general/wheelZoomFactor"), kZoomNormal).toDouble());
    if (zoom->currentIndex() < 0) zoom->setCurrentIndex(1);
    QObject::connect(zoom, &QComboBox::currentIndexChanged, zoom, [zoom, changed] {
        QSettings().setValue(QStringLiteral("general/wheelZoomFactor"), zoom->currentData().toDouble());
        if (changed) changed(Change::ZoomSpeed);
    });
    f->addRow(Text::tr("Zoom speed:"), help::withHelp(zoom, QStringLiteral("prefs.zoomSpeed"), parent));

    into->addWidget(help::withHelp(
        checkbox(parent, Text::tr("Always show connection points, not only on the picked pieces"),
                 QStringLiteral("appearance/alwaysShowConnections"), false,
                 [changed] { if (changed) changed(Change::MapLook); }),
        QStringLiteral("prefs.connectionPoints"), parent));
}

void addLanguage(QVBoxLayout* into, QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->setObjectName(QStringLiteral("general/language"));
    combo->setAccessibleName(Text::tr("Language"));
    for (const Language& l : languages()) combo->addItem(l.name, l.code);
    // Unset (or empty): Match my computer, as the app starts with.
    const QString current = QSettings().value(QStringLiteral("general/language")).toString();
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo] {
        QSettings().setValue(QStringLiteral("general/language"), combo->currentData().toString());
    });
    into->addWidget(help::withHelp(combo, QStringLiteral("prefs.language"), parent));
    into->addWidget(muted(Text::tr("A new language shows the next time the app starts."), parent));
}

void addStartingUp(QVBoxLayout* into, QWidget* parent) {
    into->addWidget(checkbox(parent, Text::tr("Open the layout I had open last time"),
                             QStringLiteral("general/reopenLastFile"), true));
    into->addWidget(checkbox(parent, Text::tr("Check for a new version (at most once a day)"),
                             QStringLiteral("updates/checkAtStartup"), true));
    QFormLayout* f = form(into);
    QLineEdit* edit = nullptr;
    QWidget* tpl = pathPicker(parent, QStringLiteral("general/newMapTemplate"), Text::tr("Layout new layouts start from"),
                              false, Text::tr("BlueBrick map (*.bbm)"), &edit);
    edit->setPlaceholderText(Text::tr("None: new layouts start empty"));
    f->addRow(Text::tr("New layouts start from:"), help::withHelp(tpl, QStringLiteral("prefs.newTemplate"), parent));
}

void addMapLabels(QVBoxLayout* into, QWidget* parent, const Changed& changed) {
    QFormLayout* f = form(into);
    const auto redraw = [changed] { if (changed) changed(Change::MapLook); };

    auto* frame = new QDoubleSpinBox(parent);
    frame->setObjectName(QStringLiteral("view/moduleFrameThickness"));
    frame->setRange(0.5, 20.0);
    frame->setSingleStep(0.5);
    frame->setDecimals(1);
    frame->setSuffix(Text::tr(" px"));
    frame->setValue(QSettings().value(QStringLiteral("view/moduleFrameThickness"), 5.0).toDouble());
    QObject::connect(frame, &QDoubleSpinBox::valueChanged, frame, [redraw](double v) {
        QSettings().setValue(QStringLiteral("view/moduleFrameThickness"), v);
        redraw();
    });
    f->addRow(Text::tr("Module frame thickness:"), frame);

    auto* name = new QDoubleSpinBox(parent);
    name->setObjectName(QStringLiteral("view/moduleLabelPercent"));
    name->setRange(5.0, 100.0);
    name->setSingleStep(5.0);
    name->setDecimals(0);
    name->setSuffix(Text::tr(" %"));
    name->setValue(QSettings().value(QStringLiteral("view/moduleLabelPercent"), 35.0).toDouble());
    name->setToolTip(Text::tr("How big a module's name is, compared with the module's longer side."));
    QObject::connect(name, &QDoubleSpinBox::valueChanged, name, [redraw](double v) {
        QSettings().setValue(QStringLiteral("view/moduleLabelPercent"), v);
        redraw();
    });
    f->addRow(Text::tr("Module name size:"), name);

    auto* venue = new QSpinBox(parent);
    venue->setObjectName(QStringLiteral("venue/labelPx"));
    venue->setRange(10, 96);
    venue->setSingleStep(2);
    venue->setSuffix(Text::tr(" px"));
    venue->setValue(QSettings().value(QStringLiteral("venue/labelPx"), 28).toInt());
    venue->setToolTip(Text::tr("How big the lengths along the venue's walls, doors and openings are."));
    QObject::connect(venue, &QSpinBox::valueChanged, venue, [redraw](int v) {
        QSettings().setValue(QStringLiteral("venue/labelPx"), v);
        redraw();
    });
    f->addRow(Text::tr("Venue measurement size:"), venue);
}

void addParts(QVBoxLayout* into, QWidget* parent, const Changed& changed,
              const std::function<void()>& openPartsFolders) {
    QFormLayout* f = form(into);
    QWidget* modules = pathPicker(parent, QStringLiteral("modules/libraryPath"), Text::tr("Module library folder"), true,
                                  {}, nullptr, [changed] { if (changed) changed(Change::ModuleFolder); });
    f->addRow(Text::tr("Module library folder:"), modules);
    if (openPartsFolders) {
        auto* row = new QHBoxLayout;
        auto* btn = new QPushButton(Text::tr("Parts Folders…"), parent);
        btn->setObjectName(QStringLiteral("partsFolders"));
        btn->setAutoDefault(false);
        QObject::connect(btn, &QPushButton::clicked, btn, [openPartsFolders] { openPartsFolders(); });
        row->addWidget(btn);
        row->addWidget(muted(Text::tr("Extra folders of parts to show in the Parts panel."), parent), 1);
        row->addWidget(new help::HelpButton(QStringLiteral("prefs.partLibraries"), parent, parent));
        into->addLayout(row);
    }
    auto* budget = new QCheckBox(Text::tr("Parts with no number in the budget can be used freely"), parent);
    budget->setObjectName(QStringLiteral("budgetUnlimited"));
    budget->setChecked(BudgetSession::defaultBudgetIsInfinite());
    QObject::connect(budget, &QCheckBox::toggled, budget, [](bool on) { BudgetSession::setDefaultBudgetIsInfinite(on); });
    into->addWidget(help::withHelp(budget, QStringLiteral("prefs.budgetUnlimited"), parent));
    into->addWidget(checkbox(parent, Text::tr("While live on a server, also list other servers' parts"),
                             QStringLiteral("parts/showAllServers"), false,
                             [changed] { if (changed) changed(Change::PartsList); }));
}

void addImport(QVBoxLayout* into, QWidget* parent) {
    QWidget* w = parent;
    QFormLayout* form = settings::form(into);
    QLineEdit* ldrawEdit = nullptr;
    QWidget* ldrawWrap = pathPicker(parent, QStringLiteral("import/ldrawLibraryPath"), Text::tr("LDraw library"), true, {},
                                    &ldrawEdit);
    ldrawEdit->setPlaceholderText(Text::tr("e.g. ~/ldraw (with LDConfig.ldr and parts/ in it)"));
    form->addRow(Text::tr("LDraw library:"), help::withHelp(ldrawWrap, QStringLiteral("prefs.ldraw"), parent));
    const auto saveLdraw = [ldrawEdit] { QSettings().setValue(QStringLiteral("import/ldrawLibraryPath"), ldrawEdit->text()); };

    // One click: the official complete archive from library.ldraw.org, into
    // the app's own folder, and the field pointed at it.
    auto* autoBtn = new QPushButton(Text::tr("Download the LDraw library…"), w);
    autoBtn->setObjectName(QStringLiteral("downloadLdraw"));
    autoBtn->setAutoDefault(false);
    autoBtn->setToolTip(Text::tr("About 140 MB from library.ldraw.org, kept in the app's own folder. Needs the internet."));
    QObject::connect(autoBtn, &QPushButton::clicked, w, [ldrawEdit, w, saveLdraw]{
        const auto btn = QMessageBox::question(w,
            Text::tr("Download LDraw library"),
            Text::tr("This will download ~140 MB from library.ldraw.org "
                        "and extract it to your app-data folder. Continue?"));
        if (btn != QMessageBox::Yes) return;

        const QString destRoot = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + QStringLiteral("/ldraw-library");
        QDir().mkpath(destRoot);

        // QNetworkAccessManager owned by the heap so its lifetime
        // outlives the lambda capture if the user closes the
        // Preferences dialog mid-download. parent = w so it gets
        // cleaned up with the widget.
        auto* nam = new QNetworkAccessManager(w);
        QNetworkRequest req(QUrl(QStringLiteral(
            "https://library.ldraw.org/library/updates/complete.zip")));
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Brick-Layout-Designer/1.0"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* rep = nam->get(req);

        // Modal-but-pumping progress dialog. setValue() pumps the
        // event loop on its own — so as long as we keep calling
        // setValue() we never block. The cancel button is wired
        // before the network reply gets to processEvents-blocking
        // state so the user can always escape.
        QProgressDialog dlg(Text::tr("Downloading LDraw library…"),
                             Text::tr("Cancel"), 0, 100, w);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumDuration(0);
        dlg.setAutoClose(false);
        dlg.setAutoReset(false);
        bool cancelled = false;
        QObject::connect(&dlg, &QProgressDialog::canceled, rep,
            [rep, &cancelled]{ cancelled = true; rep->abort(); });
        QObject::connect(rep, &QNetworkReply::downloadProgress, &dlg,
            [&dlg](qint64 done, qint64 total){
                constexpr qint64 kMegabyte = qint64{1024} * 1024;
                if (total <= 0) { dlg.setRange(0, 0); return; }
                const int mb = std::max(1, static_cast<int>(total / kMegabyte));
                dlg.setMaximum(mb);
                dlg.setValue(static_cast<int>(done / kMegabyte));
                dlg.setLabelText(Text::tr("Downloading LDraw library — %1 / %2 MB")
                    .arg(done / kMegabyte).arg(mb));
            });
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        // Show explicitly so it appears immediately rather than after
        // the minimum-duration delay.
        dlg.show();
        loop.exec();

        if (cancelled) {
            dlg.close();
            rep->deleteLater();
            nam->deleteLater();
            return;
        }
        if (rep->error() != QNetworkReply::NoError) {
            dlg.close();
            QMessageBox::warning(w, Text::tr("Download failed"), rep->errorString());
            rep->deleteLater();
            nam->deleteLater();
            return;
        }
        QByteArray bytes = rep->readAll();
        rep->deleteLater();
        nam->deleteLater();
        if (bytes.isEmpty()) { dlg.close(); return; }

        // One file at a time so the progress dialog stays alive and
        // cancellable: the LDraw archive has ~20 000 entries.
        const import::SafeZip zip(bytes);
        if (!zip.isValid()) {
            dlg.close();
            QMessageBox::warning(w, Text::tr("Extract failed"),
                Text::tr("Couldn't read the LDraw archive."));
            return;
        }
        const auto& entries = zip.entries();
        dlg.setRange(0, entries.size());
        dlg.setValue(0);
        dlg.setLabelText(Text::tr("Extracting %1 files…").arg(entries.size()));
        QApplication::processEvents();

        QDir dst(destRoot);
        cancelled = false;
        QObject::disconnect(&dlg, &QProgressDialog::canceled, nullptr, nullptr);
        QObject::connect(&dlg, &QProgressDialog::canceled, &dlg,
            [&cancelled]{ cancelled = true; });

        int failed = 0;
        for (int i = 0; i < entries.size(); ++i) {
            if (cancelled) break;
            const auto& entry = entries[i];
            // Drops "../" / absolute entries (zip-slip) and never recreates
            // symlink entries, which could redirect later writes.
            const QString abs = import::resolveArchiveEntryPath(dst.absolutePath(), entry.name);
            if (abs.isEmpty() || entry.isSymLink) {
                ++failed;
            } else if (entry.isDir) {
                QDir().mkpath(abs);
            } else {
                const auto data = zip.read(entry);
                QDir().mkpath(QFileInfo(abs).absolutePath());
                QFile out(abs);
                if (!data || !out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(*data) < 0)
                    ++failed;
            }
            // Update + pump every ~64 files so the dialog stays
            // responsive without crawling the whole loop with
            // synchronous event pumps.
            if ((i & 63) == 0) {
                dlg.setValue(i);
                QApplication::processEvents();
            }
        }
        dlg.setValue(entries.size());
        if (cancelled) {
            dlg.close();
            QMessageBox::information(w, Text::tr("Download canceled"),
                Text::tr("Extraction canceled — partial files may remain at %1")
                    .arg(destRoot));
            return;
        }
        dlg.close();

        // The archive expands to <destRoot>/ldraw/parts, /p, LDConfig.ldr —
        // point the path field one level deeper.
        const QString ldrawRoot = QDir(destRoot).filePath(QStringLiteral("ldraw"));
        if (QFileInfo::exists(QDir(ldrawRoot).filePath(QStringLiteral("LDConfig.ldr")))) {
            ldrawEdit->setText(ldrawRoot);
        } else {
            // Fallback: if the archive happens to be flat (no top
            // ldraw/ wrapper), use destRoot directly.
            ldrawEdit->setText(destRoot);
        }
        QString msg = Text::tr("LDraw library installed at:\n%1").arg(ldrawEdit->text());
        if (failed > 0) {
            msg += QStringLiteral("\n\n")
                 + Text::tr("Archive entries not written (write error or unsafe path): %1")
                       .arg(failed);
        }
        saveLdraw();
        QMessageBox::information(w, Text::tr("LDraw library installed"), msg);
    });
    form->addRow(QString(), autoBtn);

    QLineEdit* studioEdit = nullptr;
    QWidget* studioWrap = pathPicker(parent, QStringLiteral("import/studioLibraryPath"), Text::tr("Studio's LDraw library"),
                                     true, {}, &studioEdit);
    // Studio's own library and mapping, found where Studio installs, are
    // used while these are left empty (and say so).
    const bool useStudio = QSettings().value(QStringLiteral("import/useStudioLibrary"), true).toBool();
    const StudioInstall studio = useStudio ? findStudioInstall() : StudioInstall{};
    studioEdit->setPlaceholderText(studio.ldrawLibrary.isEmpty()
                                       ? Text::tr("Studio's own, else the LDraw library above")
                                       : Text::tr("Found Studio's: %1").arg(QDir::toNativeSeparators(studio.ldrawLibrary)));
    if (!studio.ldrawLibrary.isEmpty() && ldrawEdit->text().isEmpty())
        ldrawEdit->setPlaceholderText(Text::tr("Using Studio's: %1").arg(QDir::toNativeSeparators(studio.ldrawLibrary)));
    form->addRow(Text::tr("Studio library:"), studioWrap);

    QLineEdit* lddEdit = nullptr;
    QWidget* lddWrap = pathPicker(parent, QStringLiteral("import/lddInstallPath"), Text::tr("LEGO Digital Designer"), true,
                                  {}, &lddEdit);
    lddEdit->setPlaceholderText(Text::tr("LDD's program folder (Assets.lif) or data folder (db.lif)"));
    form->addRow(Text::tr("LEGO Digital Designer:"), lddWrap);

    // LDD ships an old ldraw.xml without e.g. train track; the community
    // keeps a much larger one up to date.
    QLineEdit* xmlEdit = nullptr;
    QWidget* xmlWrap = pathPicker(parent, QStringLiteral("import/lddLdrawXml"), Text::tr("LDD to LDraw part list"), false,
                                  Text::tr("ldraw.xml (*.xml)"), &xmlEdit);
    xmlEdit->setPlaceholderText(studio.ldrawXml.isEmpty()
                                    ? Text::tr("Optional: a newer ldraw.xml, e.g. from slswww.free.fr/ldraw.xml")
                                    : Text::tr("Using Studio's: %1").arg(QDir::toNativeSeparators(studio.ldrawXml)));
    form->addRow(Text::tr("LDD to LDraw part list:"), xmlWrap);
    into->addWidget(checkbox(parent, Text::tr("Use Studio's LDraw library and part list when these are left empty"),
                             QStringLiteral("import/useStudioLibrary"), true));
}

}  // namespace bld::ui::settings
