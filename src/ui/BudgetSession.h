#pragma once

#include "../edit/Budget.h"

#include <QObject>
#include <QString>

#include <optional>

namespace bld::core  { class Map; }
namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

class MapView;

// The open layout's budget (sidecar.budgetLimits): how many of each part
// it may use. It is saved with the layout and shared with everyone who
// edits it (the web keeps it at meta.budgetLimits), and every change is an
// undo step on the layout. Shared by the parts browser, the part-usage
// panel, the status bar and placement (when Stop at the Budget Limits is
// on). BlueBrick's .bbb files are imported and exported here.
class BudgetSession : public QObject {
    Q_OBJECT
public:
    explicit BudgetSession(parts::PartsLibrary& lib, QObject* parent = nullptr);

    // The view whose layout the budget belongs to; follows it from then on.
    void attach(MapView* view);
    // Read the budget from the layout again (it changed by undo, redo or
    // someone else); says so when it differs.
    void reload() { refresh(false); }

    bool exists() const { return budget_.has_value(); }
    const edit::Budget* budget() const { return budget_ ? &*budget_ : nullptr; }
    // Whether the layout can be changed here (one is open).
    bool canEdit() const;

    // Start a budget with no limits yet; take the layout's budget away.
    void create();
    void remove();
    // A .bbb file's limits as the layout's budget; or added to it (BlueBrick's Import and Merge).
    bool importFile(const QString& path, QString* error = nullptr);
    bool importAndMerge(const QString& path, QString* error = nullptr);
    // The budget as a .bbb file, for BlueBrick or another layout.
    bool exportFile(const QString& path, QString* error = nullptr);
    // The folder the last .bbb was imported from or exported to, for the file dialogs.
    static QString lastFileDir();

    // Older versions kept one budget file for the whole app and opened it
    // at every start. The first start of a version that keeps the budget in
    // the layout notes that file here (once, whatever happens later), so the
    // window can offer to bring it into a layout. Empty when there was none,
    // the file is gone, or the offer was made.
    static QString oldAppWideFile();
    // The offer was made: never again.
    static void forgetOldAppWideFile();

    // Per-part limit (-1 = unlimited). Setting a negative value clears it.
    int limit(const QString& part) const;
    void setLimit(const QString& part, int limit);
    // BlueBrick's IsBudgeted: a positive limit.
    bool isBudgeted(const QString& part) const { return exists() && limit(part) > 0; }

    // Budget menu options (remembered across sessions); only apply while a
    // budget exists.
    bool showOnlyBudgetedParts() const;
    bool showBudgetNumbers() const;
    bool useBudgetLimitation() const;
    void setShowOnlyBudgetedParts(bool on);
    void setShowBudgetNumbers(bool on);
    void setUseBudgetLimitation(bool on);
    // Preferences: parts without a limit are unlimited (true) or forbidden.
    static bool defaultBudgetIsInfinite();
    static void setDefaultBudgetIsInfinite(bool on);

    // Whether placing `quantity` of `part` (a set counts its parts too)
    // keeps within the budget. Always true without Use Budget Limitation.
    bool canAdd(const core::Map& map, const QString& part, int quantity = 1) const;

signals:
    void changed();

private:
    core::Map* map() const;
    // `force`: say so even when the budget is the same (another layout was opened).
    void refresh(bool force);
    // Make `next` the layout's budget, as one undo step named `text`.
    void apply(const std::optional<edit::Budget>& next, const QString& text);

    parts::PartsLibrary& lib_;
    MapView* view_ = nullptr;
    std::optional<edit::Budget> budget_;
};

}  // namespace bld::ui
