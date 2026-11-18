#pragma once

// Modules › Updates from Shared Modules… (docs/shared-modules.md, "Layout
// settings"; the web's SharedUpdatesDialog.tsx): whether this layout asks
// first before a shared module's new version goes in, or takes each one
// automatically as soon as its owner saves it. It is the server's setting
// for a layout on a server; whoever may edit the layout changes it, and a
// change made elsewhere shows here at once (setCurrent).

#include <QDialog>
#include <QString>

class QLabel;
class QRadioButton;

namespace bld::ui {

class SharedUpdatesDialog : public QDialog {
    Q_OBJECT
public:
    explicit SharedUpdatesDialog(QWidget* parent = nullptr);

    // "ask" or "auto"; empty while it isn't known yet (the choices wait).
    void setCurrent(const QString& updates);
    QString current() const { return current_; }
    // Can't change the layout: the choice is shown, not offered.
    void setCanChange(bool can);
    // Being sent to the server.
    void setBusy(bool busy);

    // For tests.
    QRadioButton* askButton() const { return ask_; }
    QRadioButton* autoButton() const { return auto_; }
    QLabel* readOnlyNote() const { return readOnly_; }

signals:
    // Someone picked the other choice ("ask" or "auto").
    void chosen(const QString& updates);

private:
    void refresh();

    QRadioButton* ask_ = nullptr;
    QRadioButton* auto_ = nullptr;
    QLabel* readOnly_ = nullptr;
    QString current_;
    bool canChange_ = false;
    bool busy_ = false;
};

}  // namespace bld::ui
