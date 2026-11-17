#pragma once

// The notices for shared modules, over the bottom of the map
// (docs/shared-modules.md, "The update notice"; the web's
// SharedModulesBar.tsx): a new version is ready (Show / Update / Not now;
// several: Update all), and, after an update, what the module is now with
// Go back and OK. Nothing updates by itself: someone who can edit the
// layout presses Update.

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace bld::ui {

class SharedModulesBar : public QWidget {
    Q_OBJECT
public:
    explicit SharedModulesBar(QWidget* parent = nullptr);

    // New versions are ready: `text` says which, `list` names them when
    // there are several (else empty), `count` how many. `key` tells this
    // set of versions from another: "Not now" hides the notice for the key
    // it was pressed on. An empty text hides it.
    void setUpdates(const QString& text, const QString& list, int count, const QString& key);
    // After an update: `text` says what the module is now; `goBackTo` (> 0)
    // offers "Go back to version N". An empty text hides it.
    void setReview(const QString& text, int goBackTo);
    // You can edit the layout: Update, Go back and OK are yours to press.
    void setCanUpdate(bool can);
    // Something is being sent to the server.
    void setBusy(bool busy);

    // Over the bottom of `area` (the map's viewport), above `above` px (another bar there).
    void place(const QRect& area, int above = 0);

    // For tests.
    bool updatesShown() const;
    bool reviewShown() const;
    QString updatesText() const;
    QString reviewText() const;
    QPushButton* updateButton() const { return update_; }
    QPushButton* showUpdatesButton() const { return showUpdates_; }
    QPushButton* notNowButton() const { return notNow_; }
    QPushButton* goBackButton() const { return goBack_; }
    QPushButton* okButton() const { return ok_; }
    QPushButton* showReviewButton() const { return showReview_; }

signals:
    void showUpdatesRequested();
    void updateRequested();
    void showReviewRequested();
    void goBackRequested();
    void okRequested();
    // The bar came, went or changed height: place it again.
    void resized();

private:
    void refresh();

    QWidget* updates_ = nullptr;
    QLabel* updatesText_ = nullptr;
    QLabel* updatesList_ = nullptr;
    QLabel* cantUpdate_ = nullptr;
    QPushButton* showUpdates_ = nullptr;
    QPushButton* update_ = nullptr;
    QPushButton* notNow_ = nullptr;
    QWidget* review_ = nullptr;
    QLabel* reviewText_ = nullptr;
    QPushButton* showReview_ = nullptr;
    QPushButton* goBack_ = nullptr;
    QPushButton* ok_ = nullptr;
    QString key_;
    QString closedFor_;
    bool hasUpdates_ = false;
    bool hasReview_ = false;
    bool canUpdate_ = false;
    bool busy_ = false;
    int count_ = 0;
    int goBackTo_ = 0;
};

}  // namespace bld::ui
