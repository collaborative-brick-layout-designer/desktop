#include "FindDialog.h"
#include "MapView.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/LayerText.h"
#include "../core/Map.h"
#include "../edit/EditCommands.h"
#include "../edit/TextCommands.h"
#include "../parts/PartsLibrary.h"
#include "ConfirmDialog.h"
#include "rendering/MapText.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>

namespace bld::ui {

FindDialog::FindDialog(MapView& view, QWidget* parent)
    : QDialog(parent), view_(view) {
    setWindowTitle(tr("Find && Replace"));
    resize(480, 240);
    setModal(false);

    auto* vbox = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    // Parts first, found by number or name, like the website's Find (its
    // FindDialog.tsx); index 0 is parts, 1 is text.
    auto* scope = new QComboBox(this);
    scope->addItem(tr("Parts (name or number)"));
    scope->addItem(tr("Text on the map"));
    form->addRow(tr("Search in:"), scope);

    auto* findE = new QLineEdit(this);
    form->addRow(tr("Find:"), findE);
    auto* replaceE = new QLineEdit(this);
    form->addRow(tr("Replace with:"), replaceE);

    auto* caseChk = new QCheckBox(tr("Match case"), this);
    form->addRow(caseChk);

    vbox->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    auto* findBtn = new QPushButton(tr("Find Next"), this);
    auto* replaceBtn = new QPushButton(tr("Replace"), this);
    auto* replaceAllBtn = new QPushButton(tr("Replace All"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(findBtn); btnRow->addWidget(replaceBtn);
    btnRow->addWidget(replaceAllBtn); btnRow->addStretch(); btnRow->addWidget(closeBtn);
    vbox->addLayout(btnRow);

    auto* status = new QLabel(this);
    status->setObjectName(QStringLiteral("findStatus"));
    status->setWordWrap(true);
    vbox->addWidget(status);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    auto matchesText = [](const QString& needle, const QString& hay, Qt::CaseSensitivity cs) {
        if (needle.isEmpty()) return false;
        return hay.contains(needle, cs);
    };

    // The search pass. Extracted so both the explicit "Find Next" button
    // and the live-update debounced retrigger can share it.
    // A part matches by its number or any of its names in the library.
    auto matchesPart = [&view, matchesText](const QString& needle, const QString& partNumber, Qt::CaseSensitivity cs) {
        if (matchesText(needle, partNumber, cs)) return true;
        const auto meta = view.partsLibrary().metadata(partNumber);
        if (!meta) return false;
        for (const auto& d : meta->descriptions)
            if (matchesText(needle, d.text, cs)) return true;
        return false;
    };

    auto runSearch = [&view, scope, findE, caseChk, status, matchesText, matchesPart]{
        auto* map = view.currentMap();
        if (!map) return;
        const Qt::CaseSensitivity cs = caseChk->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const QString needle = findE->text();
        if (needle.isEmpty()) { view.deselectAll(); status->clear(); return; }

        view.deselectAll();
        int matches = 0;
        QList<QGraphicsItem*> pick;
        for (const auto& it : view.scene()->items()) {
            const int li = it->data(0).toInt();
            const QString guid = it->data(1).toString();
            const QString kind = it->data(2).toString();
            if (scope->currentIndex() == 1 && kind == QStringLiteral("text")) {
                auto* L = map->layers()[li].get();
                if (!L) continue;
                for (const auto& c : static_cast<core::LayerText&>(*L).textCells) {
                    if (c.guid == guid && matchesText(needle, c.text, cs)) {
                        pick.append(it); ++matches;
                        break;
                    }
                }
            } else if (scope->currentIndex() == 0 && kind == QStringLiteral("brick")) {
                auto* L = map->layers()[li].get();
                if (!L) continue;
                for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                    if (b.guid == guid && matchesPart(needle, b.partNumber, cs)) {
                        pick.append(it); ++matches;
                        break;
                    }
                }
            }
        }
        view.selectItems(pick, false);
        status->setText(QObject::tr("%1 match(es).").arg(matches));
    };
    connect(findBtn, &QPushButton::clicked, this, runSearch);

    // Live-update: debounce at 200 ms so typing doesn't redo the scan on
    // every keystroke but still feels responsive. Triggered from every
    // input that affects the result set (text, scope, case).
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(200);
    connect(debounce, &QTimer::timeout, this, runSearch);
    auto schedule = [debounce]{ debounce->start(); };
    connect(findE, &QLineEdit::textChanged, this, [schedule](const QString&){ schedule(); });
    connect(scope, &QComboBox::currentIndexChanged, this, [schedule](int){ schedule(); });
    connect(caseChk, &QCheckBox::toggled, this, [schedule](bool){ schedule(); });

    // Re-run the search whenever the map mutates (e.g. user edits a text
    // cell while the dialog is open), so the match list stays accurate.
    connect(view.undoStack(), &QUndoStack::indexChanged, this,
            [schedule](int){ schedule(); });

    // What a part becomes: the found text replaced in its number, or, for a
    // part found by its name, the whole number. Empty: no change.
    auto newPartNumber = [](const QString& partNumber, const QString& needle, const QString& with, Qt::CaseSensitivity cs) {
        QString next = partNumber;
        if (partNumber.contains(needle, cs)) next.replace(needle, with, cs);
        else next = with.trimmed();
        return next == partNumber || next.trimmed().isEmpty() ? QString() : next;
    };

    auto doReplace = [this, &view, scope, findE, replaceE, caseChk, status, matchesPart, newPartNumber](bool all) {
        auto* map = view.currentMap();
        if (!map) return;
        const Qt::CaseSensitivity cs = caseChk->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const QString needle = findE->text();
        if (needle.isEmpty()) return;
        const QString with = replaceE->text();

        // A part number the parts library doesn't have draws as a red ✕: ask first.
        if (scope->currentIndex() == 0) {
            QStringList unknown;
            for (const auto& L : map->layers()) {
                if (!L || L->kind() != core::LayerKind::Brick) continue;
                for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
                    if (!matchesPart(needle, b.partNumber, cs)) continue;
                    const QString next = newPartNumber(b.partNumber, needle, with, cs);
                    if (!next.isEmpty() && !view.partsLibrary().metadata(next) && !unknown.contains(next)) unknown << next;
                    if (!all) break;
                }
                if (!all && !unknown.isEmpty()) break;
            }
            if (!unknown.isEmpty()) {
                QStringList shown;
                for (int i = 0; i < unknown.size() && i < 3; ++i) shown << QStringLiteral("“%1”").arg(unknown[i]);
                QString list = shown.join(QStringLiteral(", "));
                if (unknown.size() > 3) list += QObject::tr(" and %1 more").arg(unknown.size() - 3);
                ConfirmOptions o;
                o.title = unknown.size() == 1 ? QObject::tr("%1 isn’t in the parts library").arg(list)
                                              : QObject::tr("These aren’t in the parts library");
                o.removes = (unknown.size() == 1 ? QString() : list + QStringLiteral(". "))
                          + QObject::tr("A part that isn’t in the parts library shows on the map as a red ✕, and can’t be counted or snapped.");
                o.keeps = QObject::tr("Check the part number in the Parts panel.");
                o.confirmLabel = QObject::tr("Replace Anyway");
                if (!ConfirmDialog::ask(this, o)) return;
            }
        }

        int replaced = 0;
        view.undoStack()->beginMacro(QObject::tr("Find & Replace"));
        if (scope->currentIndex() == 1) {
            for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
                auto* L = map->layers()[li].get();
                if (!L || L->kind() != core::LayerKind::Text) continue;
                for (const auto& c : static_cast<core::LayerText&>(*L).textCells) {
                    if (!c.text.contains(needle, cs)) continue;
                    QString next = c.text; next.replace(needle, with, cs);
                    const QRectF area =
                        rendering::refittedTextArea(c.displayArea, next, c.font.sizePt, c.orientation,
                                                    rendering::mapLineWidth(c.font.styleString));
                    view.undoStack()->push(
                        new edit::EditTextCellTextCommand(*map, li, c.guid, next, nullptr, area));
                    ++replaced;
                    if (!all) break;
                }
                if (!all && replaced > 0) break;
            }
        } else {
            for (int li = 0; li < static_cast<int>(map->layers().size()); ++li) {
                auto* L = map->layers()[li].get();
                if (!L || L->kind() != core::LayerKind::Brick) continue;
                for (const auto& b : static_cast<core::LayerBrick&>(*L).bricks) {
                    if (!matchesPart(needle, b.partNumber, cs)) continue;
                    const QString next = newPartNumber(b.partNumber, needle, with, cs);
                    if (next.isEmpty()) continue;
                    edit::EditBrickCommand::State before{
                        b.partNumber, b.displayArea.topLeft(),
                        b.orientation, b.altitude, b.activeConnectionPointIndex };
                    edit::EditBrickCommand::State after = before;
                    after.partNumber = next;
                    view.undoStack()->push(new edit::EditBrickCommand(
                        *map, edit::BrickRef{ li, b.guid }, before, after));
                    ++replaced;
                    if (!all) break;
                }
                if (!all && replaced > 0) break;
            }
        }
        view.undoStack()->endMacro();
        const bool parts = scope->currentIndex() == 0;
        status->setText(replaced == 0 ? QObject::tr("Nothing changed.")
                        : parts ? (replaced == 1 ? QObject::tr("Replaced 1 part.") : QObject::tr("Replaced %1 parts.").arg(replaced))
                                : (replaced == 1 ? QObject::tr("Replaced 1 text.") : QObject::tr("Replaced %1 texts.").arg(replaced)));
    };
    connect(replaceBtn, &QPushButton::clicked, this, [doReplace]{ doReplace(false); });
    connect(replaceAllBtn, &QPushButton::clicked, this, [doReplace]{ doReplace(true); });
}

}
