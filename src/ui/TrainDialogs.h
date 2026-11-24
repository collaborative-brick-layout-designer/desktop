#pragma once

// The Trains panel's dialogs (trains design 6.4; the web's StockEditor.tsx
// and TrainsPanel.tsx): one car, made or changed (a quick car needs only a
// number and a length; More details opens the trucks, couplers, power,
// the track it runs on, a part's picture and notes); one train's cars in
// order; and a roster's cars to bring into the layout.

#include "../core/Train.h"
#include "../sync/LibraryApi.h"

#include <QDialog>
#include <QList>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QWidget;

namespace bld::parts { class PartsLibrary; }

namespace bld::ui {

// Plain words for each kind of stock (the record keeps the key).
QString stockKindLabel(const QString& kind);
// "4021 Boxcar", or its kind when it has neither.
QString stockLabel(const core::RollingStock& s);
// Change a car's length, keeping its trucks the same share in from each end.
core::RollingStock withLength(core::RollingStock s, double length);

class StockDialog : public QDialog {
    Q_OBJECT
public:
    StockDialog(const core::RollingStock& initial, bool isNew, parts::PartsLibrary& parts, QWidget* parent = nullptr);
    core::RollingStock stock() const;
    // Whether the trucks are where a car can have them (Save waits otherwise).
    bool valid() const;
    // The fields (tests).
    QLineEdit* numberEdit() const { return number_; }
    QDoubleSpinBox* lengthEdit() const { return length_; }
    QDoubleSpinBox* frontTruckEdit() const { return frontTruck_; }
    QDoubleSpinBox* rearTruckEdit() const { return rearTruck_; }
    QPushButton* moreButton() const { return more_; }
    QWidget* moreFields() const { return moreBox_; }
    QPushButton* saveButton() const { return save_; }

private:
    void lengthChanged(double v);
    void check();
    void fillParts(const QString& query);
    core::RollingStock base_;
    parts::PartsLibrary& parts_;
    QLineEdit* number_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* kind_ = nullptr;
    QPushButton* color_ = nullptr;
    QString colorName_;
    QDoubleSpinBox* length_ = nullptr;
    QLabel* lengthHint_ = nullptr;
    QPushButton* more_ = nullptr;
    QWidget* moreBox_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* body_ = nullptr;
    QDoubleSpinBox* frontTruck_ = nullptr;
    QDoubleSpinBox* rearTruck_ = nullptr;
    QComboBox* frontCoupler_ = nullptr;
    QComboBox* rearCoupler_ = nullptr;
    QCheckBox* powered_ = nullptr;
    QComboBox* runsOn_ = nullptr;
    QRadioButton* lookSimple_ = nullptr;
    QRadioButton* lookPart_ = nullptr;
    QWidget* partBox_ = nullptr;
    QLineEdit* partSearch_ = nullptr;
    QListWidget* partList_ = nullptr;
    QString partKey_;
    QPlainTextEdit* notes_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* save_ = nullptr;
};

class TrainDialog : public QDialog {
    Q_OBJECT
public:
    TrainDialog(const core::Train& initial, const std::vector<core::RollingStock>& stock, QWidget* parent = nullptr);
    core::Train train() const;
    // What the buttons do (tests).
    void addUnit(const QString& stockId);
    void moveUnit(int i, int by);
    void turnUnit(int i);
    void removeUnit(int i);
    QLineEdit* nameEdit() const { return name_; }
    QPushButton* saveButton() const { return save_; }
    QSpinBox* speedBox() const { return speed_; }

private:
    void rebuild();
    core::Train t_;
    std::vector<core::RollingStock> stock_;
    QLineEdit* name_ = nullptr;
    QSpinBox* speed_ = nullptr;
    QListWidget* list_ = nullptr;
    QComboBox* add_ = nullptr;
    QLabel* length_ = nullptr;
    QPushButton* save_ = nullptr;
};

// Your roster and your clubs': Add a car to this layout, or Update the layout's copy.
class RosterDialog : public QDialog {
    Q_OBJECT
public:
    RosterDialog(const QList<sync::RosterStock>& entries, const std::vector<core::RollingStock>& stock, QWidget* parent = nullptr);
    // The entries picked, Add (no copy yet) or Update (an older copy).
    void setEntries(const QList<sync::RosterStock>& entries, const std::vector<core::RollingStock>& stock);
signals:
    void addRequested(const sync::RosterStock& entry);
    // Take an entry off the roster (yours, or a club's you may change); layouts keep their copies.
    void removeRequested(const sync::RosterStock& entry);

private:
    QWidget* body_ = nullptr;
    QLabel* empty_ = nullptr;
};

}  // namespace bld::ui
