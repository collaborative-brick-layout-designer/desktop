#pragma once

// Trains › Import a model… (trains design 3.3, Phase 6): an imported model
// as an engine or car. The model from above, front to the right, with a
// marker on each truck and coupler where the importer guessed them (from the
// model's bogies, wheels and magnets); drag a marker, or type the number, to
// fix one, and Turn around if the other end is the front. The web's
// apps/web/src/import/StockSetup.tsx draws the same.

#include "ImportPipeline.h"

#include "../core/Train.h"
#include "../track/StockGuess.h"

#include <QDialog>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;

namespace bld::ui {

// The words under the picture: what the guess came from.
QString stockGuessNote(const track::StockGuess& g);

// The guess for a prepared model: the importer's, else one from its footprint
// alone (trucks a quarter of the way in from each end).
track::StockGuess stockGuessOf(const PreparedPart& part);

// What the dialog makes: the car's fields as confirmed.
struct StockSetup {
    double length = 32.0;
    double width = 6.0;
    double bodyLength = 30.0;
    double front = 8.0; // trucks: studs from the middle, + towards the front
    double rear = -8.0;
};

// An engine or car drawn with the imported part `partKey` (look mode
// "model"), its trucks and couplers as set up, `model` naming the file it
// came from. The web's stockFromModel (apps/web/src/editor/trains.ts) makes the same.
core::RollingStock stockFromModel(const QString& id, const QString& partKey, const StockSetup& setup,
                                  const QString& number, const QString& kind, const QString& file,
                                  const QString& format, const QString& color, const QString& date);

class StockImportDialog : public QDialog {
    Q_OBJECT
public:
    // `part` as prepared (not turned yet); `guess` from stockGuessOf. `canRoster`: signed in, so it may go on
    // a roster too.
    StockImportDialog(PreparedPart part, const track::StockGuess& guess, bool canRoster,
                      QWidget* parent = nullptr);

    // The part as it will be written: turned to run front to the right.
    const PreparedPart& part() const { return part_; }
    StockSetup setup() const { return setup_; }
    QString partName() const;
    QString number() const;
    QString kind() const;
    bool toRoster() const;

    // What the controls do, callable directly (tests).
    void setFront(double at);
    void setRear(double at);
    void setLength(double length);
    void turnAround();
    // Where a marker is drawn, studs along the car (tests).
    double frontMarker() const;
    double rearMarker() const;
    double couplerMarker() const;
    QGraphicsView* view() const { return view_; }
    QPushButton* addButton() const { return add_; }
    QLabel* note() const { return note_; }

private:
    void redraw();
    void syncFields();
    PreparedPart part_;
    StockSetup setup_;
    bool syncing_ = false;
    QGraphicsScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    QGraphicsPixmapItem* picture_ = nullptr;
    QGraphicsRectItem* body_ = nullptr;
    QGraphicsRectItem* frontItem_ = nullptr;
    QGraphicsRectItem* rearItem_ = nullptr;
    QGraphicsRectItem* couplerFront_ = nullptr;
    QGraphicsRectItem* couplerRear_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* number_ = nullptr;
    QComboBox* kind_ = nullptr;
    QDoubleSpinBox* frontBox_ = nullptr;
    QDoubleSpinBox* rearBox_ = nullptr;
    QDoubleSpinBox* lengthBox_ = nullptr;
    QCheckBox* roster_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* add_ = nullptr;
};

} // namespace bld::ui
