#include "BudgetSession.h"

#include "MapView.h"

#include "../core/Map.h"
#include "../core/Sidecar.h"
#include "../edit/BudgetCommands.h"
#include "../parts/PartsLibrary.h"

#include <QFileInfo>
#include <QSettings>
#include <QUndoStack>

namespace bld::ui {

namespace {

const QString kLastFile      = QStringLiteral("budget/lastFile");
const QString kOnlyBudgeted  = QStringLiteral("budget/showOnlyBudgetedParts");
const QString kNumbers       = QStringLiteral("budget/showBudgetNumbers");
const QString kLimitation    = QStringLiteral("budget/useBudgetLimitation");
const QString kDefaultInfinite = QStringLiteral("budget/defaultInfinite");
// The app-wide budget file of older versions: looked for once (kOldChecked),
// and kept in kOldFile until the offer to bring it in is made.
const QString kOldChecked    = QStringLiteral("budget/oldFileChecked");
const QString kOldFile       = QStringLiteral("budget/oldFile");

// The layout's limits as a Budget, and back.
std::optional<edit::Budget> budgetOf(const std::optional<QMap<QString, int>>& limits) {
    if (!limits) return std::nullopt;
    edit::Budget b;
    for (auto it = limits->constBegin(); it != limits->constEnd(); ++it) b.setLimit(it.key(), it.value());
    return b;
}

std::optional<QMap<QString, int>> limitsOf(const std::optional<edit::Budget>& budget) {
    if (!budget) return std::nullopt;
    QMap<QString, int> out;
    for (const auto& e : budget->entries()) out.insert(e.first.toUpper(), e.second);
    return out;
}

}  // namespace

BudgetSession::BudgetSession(parts::PartsLibrary& lib, QObject* parent) : QObject(parent), lib_(lib) {}

core::Map* BudgetSession::map() const { return view_ ? view_->currentMap() : nullptr; }

bool BudgetSession::canEdit() const { return map() != nullptr; }

void BudgetSession::attach(MapView* view) {
    view_ = view;
    if (!view_) return;
    connect(view_, &MapView::mapLoaded, this, [this] { refresh(true); });
    connect(view_->undoStack(), &QUndoStack::indexChanged, this, [this](int) { refresh(false); });
    refresh(true);
}

void BudgetSession::refresh(bool force) {
    const core::Map* m = map();
    const std::optional<QMap<QString, int>> now = m ? m->sidecar.budgetLimits : std::nullopt;
    if (!force && now == limitsOf(budget_)) return;
    budget_ = budgetOf(now);
    emit changed();
}

void BudgetSession::apply(const std::optional<edit::Budget>& next, const QString& text) {
    core::Map* m = map();
    if (!m) return;
    const auto want = limitsOf(next);
    if (want == m->sidecar.budgetLimits) return;
    // The undo stack's indexChanged brings reload(), which tells everyone.
    view_->undoStack()->push(new edit::SetBudgetCommand(*m, want, text));
    reload();
}

void BudgetSession::create() { apply(edit::Budget{}, tr("New budget")); }

void BudgetSession::remove() { apply(std::nullopt, tr("Remove budget")); }

bool BudgetSession::importFile(const QString& path, QString* error) {
    auto loaded = edit::Budget::read(path, &lib_);
    if (!loaded) {
        if (error) *error = tr("%1 is not a BlueBrick budget file.").arg(QFileInfo(path).fileName());
        return false;
    }
    QSettings().setValue(kLastFile, path);
    apply(loaded, tr("Import budget"));
    return true;
}

bool BudgetSession::importAndMerge(const QString& path, QString* error) {
    auto loaded = edit::Budget::read(path, &lib_);
    if (!loaded) {
        if (error) *error = tr("%1 is not a BlueBrick budget file.").arg(QFileInfo(path).fileName());
        return false;
    }
    QSettings().setValue(kLastFile, path);
    // As BlueBrick: the imported limits and the current ones add up.
    if (budget_) loaded->mergeWith(*budget_);
    apply(loaded, tr("Import and merge budget"));
    return true;
}

bool BudgetSession::exportFile(const QString& path, QString* error) {
    if (!budget_) return false;
    if (!budget_->write(path)) {
        if (error) *error = tr("Cannot save the budget to %1.").arg(path);
        return false;
    }
    QSettings().setValue(kLastFile, path);
    return true;
}

QString BudgetSession::lastFileDir() {
    const QString last = QSettings().value(kLastFile).toString();
    return last.isEmpty() ? QString() : QFileInfo(last).absolutePath();
}

QString BudgetSession::oldAppWideFile() {
    QSettings settings;
    if (!settings.value(kOldChecked, false).toBool()) {
        // Until now budget/lastFile was the app's budget, opened at every
        // start; from here on it is only where the file dialogs start.
        settings.setValue(kOldChecked, true);
        const QString last = settings.value(kLastFile).toString();
        if (!last.isEmpty()) settings.setValue(kOldFile, last);
    }
    QString old = settings.value(kOldFile).toString();
    if (old.isEmpty()) return {};
    if (!QFileInfo(old).isFile()) {
        settings.remove(kOldFile);
        return {};
    }
    return old;
}

void BudgetSession::forgetOldAppWideFile() { QSettings().remove(kOldFile); }

int BudgetSession::limit(const QString& part) const {
    return budget_ ? budget_->limit(part, defaultBudgetIsInfinite()) : -1;
}

void BudgetSession::setLimit(const QString& part, int limit) {
    if (!budget_) return;
    edit::Budget next = *budget_;
    if (!next.setLimit(part, limit)) return;
    apply(next, tr("Change budget"));
}

bool BudgetSession::showOnlyBudgetedParts() const { return QSettings().value(kOnlyBudgeted, false).toBool(); }
bool BudgetSession::showBudgetNumbers() const     { return QSettings().value(kNumbers, false).toBool(); }
bool BudgetSession::useBudgetLimitation() const   { return QSettings().value(kLimitation, false).toBool(); }

void BudgetSession::setShowOnlyBudgetedParts(bool on) { QSettings().setValue(kOnlyBudgeted, on); emit changed(); }
void BudgetSession::setShowBudgetNumbers(bool on)     { QSettings().setValue(kNumbers, on); emit changed(); }
void BudgetSession::setUseBudgetLimitation(bool on)   { QSettings().setValue(kLimitation, on); emit changed(); }

bool BudgetSession::defaultBudgetIsInfinite() { return QSettings().value(kDefaultInfinite, true).toBool(); }
void BudgetSession::setDefaultBudgetIsInfinite(bool on) { QSettings().setValue(kDefaultInfinite, on); }

bool BudgetSession::canAdd(const core::Map& map, const QString& part, int quantity) const {
    if (!budget_ || !useBudgetLimitation()) return true;
    return edit::canAddToBudget(*budget_, edit::countPartUsage(map), part, quantity, defaultBudgetIsInfinite(),
                                [this](const QString& p) { return edit::setLeafParts(lib_, p); });
}

}  // namespace bld::ui
