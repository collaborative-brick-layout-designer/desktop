#pragma once

// The fit checks as a list (trains design 3.5, 6.2; the web's
// FitChecksList.tsx): each one a sentence, marked as a problem (red) or a
// warning (amber), with Show me, which brings the spot into view on the
// map. Nothing is blocked: they're there to read.

#include "../track/FitChecks.h"

#include <QFrame>
#include <QPointF>
#include <QString>

#include <vector>

class QLabel;
class QVBoxLayout;

namespace bld::core { class Map; }

namespace bld::ui {

// What a check says, in plain words (names from the map, lengths in the account's unit).
QString fitCheckText(const track::FitCheck& c, const core::Map& map);

class FitChecksList : public QFrame {
    Q_OBJECT
public:
    explicit FitChecksList(const QString& title, QWidget* parent = nullptr);
    // The checks to list, and what to say when there are none (empty: the list hides itself then).
    void setChecks(const std::vector<track::FitCheck>& checks, const core::Map& map, const QString& allClear);
    int count() const { return count_; }
    // Each line's words (tests).
    QStringList lines() const;

signals:
    void showRequested(QPointF studs);

private:
    QLabel* title_ = nullptr;
    QLabel* clear_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    QString titleText_;
    int count_ = 0;
};

}  // namespace bld::ui
