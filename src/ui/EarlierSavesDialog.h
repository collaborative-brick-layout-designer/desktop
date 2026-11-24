#pragma once

// File › Earlier Saves…: the copies FileBackups kept of a layout file each
// time it was saved over, newest first, for the open file (or any other
// file that has some, picked at the top). Open as a Copy opens the picked
// one as a new, unsaved layout, leaving the file as it is; Put Back…
// writes it over the file, after keeping the file as it is (so that can be
// put back too); Show Folder opens where they are kept.

#include "FileBackups.h"

#include <QDialog>

#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace bld::ui {

class EarlierSavesDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { None, OpenCopy, PutBack };

    // `currentFile`: the open layout's file, listed first (may be empty).
    explicit EarlierSavesDialog(const QString& currentFile, QWidget* parent = nullptr);

    Action action() const { return action_; }
    // The copy picked, and the file it is of.
    std::optional<SavedCopy> chosen() const { return chosen_; }
    QString file() const;

    // How Put Back asks first, and how the folder is opened (tests answer instead).
    static std::function<bool(QWidget*, const QString& fileName, const QString& when)> confirmPutBack;
    static std::function<void(const QString& folder)> openFolder;

private:
    void fillFiles(const QString& currentFile);
    void fillCopies();
    void updateButtons();

    QList<BackedUpFile> files_;
    QList<SavedCopy> copies_;
    Action action_ = Action::None;
    std::optional<SavedCopy> chosen_;
    QComboBox* fileBox_ = nullptr;
    QLabel* intro_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* putBack_ = nullptr;
    QPushButton* folder_ = nullptr;
};

} // namespace bld::ui
