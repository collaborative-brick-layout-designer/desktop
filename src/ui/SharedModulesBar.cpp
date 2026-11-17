#include "SharedModulesBar.h"

#include "SetUpdatesBar.h"
#include "help/HelpButton.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace bld::ui {

namespace {
// One rounded card of the bar (the set updates bar's look).
class Card : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const double radius = std::min(r.height() / 2, 14.0);
        path.addRoundedRect(r, radius, radius);
        QColor fill = palette().color(QPalette::Window);
        fill.setAlpha(245);
        p.fillPath(path, fill);
        p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
        p.drawPath(path);
    }
};

QPushButton* button(const QString& text, const QString& name, QWidget* parent, bool accent = false) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setFocusPolicy(Qt::NoFocus);
    if (accent) b->setProperty("accent", true);
    return b;
}
}  // namespace

SharedModulesBar::SharedModulesBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SharedModulesBar"));
    setAccessibleName(tr("Shared modules"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);

    review_ = new Card(this);
    review_->setObjectName(QStringLiteral("sharedModuleUpdated"));
    {
        auto* row = new QHBoxLayout(review_);
        row->setContentsMargins(16, 6, 8, 6);
        row->setSpacing(8);
        reviewText_ = new QLabel(review_);
        reviewText_->setObjectName(QStringLiteral("sharedReviewText"));
        reviewText_->setTextFormat(Qt::PlainText);
        reviewText_->setWordWrap(true);
        row->addWidget(reviewText_, 1);
        showReview_ = button(SetUpdatesBar::tr("Show"), QStringLiteral("sharedReviewShow"), review_);
        showReview_->setToolTip(SetUpdatesBar::tr("Pick them on the map"));
        row->addWidget(showReview_);
        goBack_ = button(QString(), QStringLiteral("sharedReviewGoBack"), review_);
        row->addWidget(goBack_);
        ok_ = button(tr("OK"), QStringLiteral("sharedReviewOk"), review_, true);
        row->addWidget(ok_);
    }
    col->addWidget(review_);

    updates_ = new Card(this);
    updates_->setObjectName(QStringLiteral("sharedModuleUpdates"));
    {
        auto* row = new QHBoxLayout(updates_);
        row->setContentsMargins(16, 6, 8, 6);
        row->setSpacing(8);
        auto* words = new QVBoxLayout();
        words->setSpacing(2);
        updatesText_ = new QLabel(updates_);
        updatesText_->setObjectName(QStringLiteral("sharedUpdatesText"));
        updatesText_->setTextFormat(Qt::PlainText);
        updatesText_->setWordWrap(true);
        words->addWidget(updatesText_);
        updatesList_ = new QLabel(updates_);
        updatesList_->setObjectName(QStringLiteral("sharedUpdatesList"));
        updatesList_->setTextFormat(Qt::PlainText);
        updatesList_->setWordWrap(true);
        updatesList_->setProperty("muted", true);
        words->addWidget(updatesList_);
        cantUpdate_ = new QLabel(tr("Someone who can change this layout can update it."), updates_);
        cantUpdate_->setObjectName(QStringLiteral("sharedUpdatesCant"));
        cantUpdate_->setWordWrap(true);
        cantUpdate_->setProperty("muted", true);
        words->addWidget(cantUpdate_);
        row->addLayout(words, 1);
        row->addWidget(new help::HelpButton(QStringLiteral("module.sharedUpdates"), updates_));
        showUpdates_ = button(SetUpdatesBar::tr("Show"), QStringLiteral("sharedUpdatesShow"), updates_);
        showUpdates_->setToolTip(SetUpdatesBar::tr("Pick them on the map"));
        row->addWidget(showUpdates_);
        update_ = button(SetUpdatesBar::tr("Update"), QStringLiteral("sharedUpdatesUpdate"), updates_, true);
        row->addWidget(update_);
        notNow_ = button(QStringLiteral("×"), QStringLiteral("sharedUpdatesClose"), updates_);
        notNow_->setToolTip(SetUpdatesBar::tr("Not now"));
        notNow_->setAccessibleName(SetUpdatesBar::tr("Not now"));
        notNow_->setFlat(true);
        row->addWidget(notNow_);
    }
    col->addWidget(updates_);

    connect(showUpdates_, &QPushButton::clicked, this, &SharedModulesBar::showUpdatesRequested);
    connect(update_, &QPushButton::clicked, this, &SharedModulesBar::updateRequested);
    connect(notNow_, &QPushButton::clicked, this, [this] {
        closedFor_ = key_;
        refresh();
    });
    connect(showReview_, &QPushButton::clicked, this, &SharedModulesBar::showReviewRequested);
    connect(goBack_, &QPushButton::clicked, this, &SharedModulesBar::goBackRequested);
    connect(ok_, &QPushButton::clicked, this, &SharedModulesBar::okRequested);
    hide();
}

void SharedModulesBar::setUpdates(const QString& text, const QString& list, int count, const QString& key) {
    key_ = key;
    hasUpdates_ = !text.isEmpty();
    count_ = count;
    updatesText_->setText(text);
    updatesList_->setText(list);
    refresh();
}

void SharedModulesBar::setReview(const QString& text, int goBackTo) {
    hasReview_ = !text.isEmpty();
    goBackTo_ = goBackTo;
    reviewText_->setText(text);
    refresh();
}

void SharedModulesBar::setCanUpdate(bool can) {
    if (can == canUpdate_) return;
    canUpdate_ = can;
    refresh();
}

void SharedModulesBar::setBusy(bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    refresh();
}

bool SharedModulesBar::updatesShown() const { return hasUpdates_ && key_ != closedFor_; }
bool SharedModulesBar::reviewShown() const { return hasReview_; }
QString SharedModulesBar::updatesText() const { return updatesText_->text(); }
QString SharedModulesBar::reviewText() const { return reviewText_->text(); }

void SharedModulesBar::refresh() {
    const bool updates = updatesShown();
    updates_->setVisible(updates);
    updatesList_->setVisible(!updatesList_->text().isEmpty());
    cantUpdate_->setVisible(!canUpdate_);
    update_->setVisible(canUpdate_);
    update_->setEnabled(!busy_);
    update_->setText(count_ <= 1 ? SetUpdatesBar::tr("Update") : SetUpdatesBar::tr("Update all %1").arg(count_));
    review_->setVisible(hasReview_);
    goBack_->setVisible(canUpdate_ && goBackTo_ > 0);
    goBack_->setEnabled(!busy_);
    goBack_->setText(tr("Go back to version %1").arg(goBackTo_));
    ok_->setVisible(canUpdate_);
    ok_->setEnabled(!busy_);
    setVisible(updates || hasReview_);
    if (isVisible()) raise();
    emit resized();
}

void SharedModulesBar::place(const QRect& area, int above) {
    const int width = std::min(area.width() - 32, 680);
    const int height = heightForWidth(width) > 0 ? heightForWidth(width) : sizeHint().height();
    resize(width, height);
    move(area.left() + (area.width() - width) / 2, area.bottom() - height - 48 - above);
}

}  // namespace bld::ui
