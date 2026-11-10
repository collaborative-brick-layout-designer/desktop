// Map menu — background color, general info, and the venue sub-menu
// (draw outline, draw-by-dimensions, add obstacle, edit properties,
// clear, venue library panel, load from file).
// Pulled out of MainWindowMenus.cpp so the remaining setupMenus()
// function focuses on File / Edit / View / Tools wiring rather than
// the venue dialog churn.

#include "MainWindow.h"
#include "ConfirmDialog.h"

#include "../edit/venue/VenueDesign.h"
#include "MapView.h"
#include "VenueLibraryPanel.h"
#include "venue/VenueDesignerDialog.h"

#include "../core/Map.h"
#include "../core/Venue.h"
#include "../edit/EditCommands.h"
#include "../edit/LayerCommands.h"
#include "../edit/VenueCommands.h"
#include "../saveload/VenueIO.h"

#include <QAction>
#include <QColorDialog>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QUndoStack>

namespace bld::ui {

void MainWindow::setupMapMenu() {
    auto* mapMenu = menuBar()->addMenu(tr("&Map"));
    auto* bgAct = mapMenu->addAction(tr("Background &Color…"));
    bgAct->setToolTip(tr("Pick the color behind the layout"));
    connect(bgAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QColor init = m->backgroundColor.color;
        const QColor c = QColorDialog::getColor(init, this, tr("Background color"),
                                                 QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        mapView_->undoStack()->push(new edit::ChangeBackgroundColorCommand(
            *m, core::ColorSpec::fromArgb(c)));
        mapView_->scene()->setBackgroundBrush(c);
    });
    auto* bgImgAct = mapMenu->addAction(tr("Background &Image…"));
    bgImgAct->setToolTip(tr("A picture shown under the layout, such as a floor plan. It's kept with the layout in this app; BlueBrick doesn't show it."));
    connect(bgImgAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Background image"));
        auto* form = new QFormLayout(&dlg);
        auto* pathEdit = new QLineEdit(m->sidecar.backgroundImagePath, &dlg);
        auto* browseBtn = new QPushButton(tr("…"), &dlg);
        auto* clearBtn  = new QPushButton(tr("Clear"), &dlg);
        pathEdit->setAccessibleName(tr("Picture"));
        browseBtn->setAccessibleName(tr("Choose the image"));
        browseBtn->setToolTip(tr("Choose the image"));
        auto* row = new QHBoxLayout();
        row->addWidget(pathEdit); row->addWidget(browseBtn); row->addWidget(clearBtn);
        auto* rowWrap = new QWidget(&dlg); rowWrap->setLayout(row);
        form->addRow(tr("Picture:"), rowWrap);
        connect(browseBtn, &QPushButton::clicked, &dlg, [pathEdit, &dlg]{
            const QString p = QFileDialog::getOpenFileName(&dlg, tr("Choose background image"),
                pathEdit->text(),
                tr("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"));
            if (!p.isEmpty()) pathEdit->setText(p);
        });
        connect(clearBtn, &QPushButton::clicked, &dlg, [pathEdit]{ pathEdit->clear(); });

        auto* opacitySpin = new QDoubleSpinBox(&dlg);
        opacitySpin->setRange(0.0, 1.0);
        opacitySpin->setSingleStep(0.05);
        opacitySpin->setValue(m->sidecar.backgroundImageOpacity);
        form->addRow(tr("Opacity:"), opacitySpin);

        auto* xSpin = new QDoubleSpinBox(&dlg);
        xSpin->setRange(-100000, 100000); xSpin->setDecimals(1);
        auto* ySpin = new QDoubleSpinBox(&dlg);
        ySpin->setRange(-100000, 100000); ySpin->setDecimals(1);
        auto* wSpin = new QDoubleSpinBox(&dlg);
        wSpin->setRange(0, 100000); wSpin->setDecimals(1);
        auto* hSpin = new QDoubleSpinBox(&dlg);
        hSpin->setRange(0, 100000); hSpin->setDecimals(1);
        const auto& r = m->sidecar.backgroundImageRectStuds;
        if (!r.isNull()) {
            xSpin->setValue(r.x()); ySpin->setValue(r.y());
            wSpin->setValue(r.width()); hSpin->setValue(r.height());
        }
        auto* rectGroupForm = new QFormLayout();
        rectGroupForm->addRow(tr("X (studs):"), xSpin);
        rectGroupForm->addRow(tr("Y (studs):"), ySpin);
        rectGroupForm->addRow(tr("Width (studs):"), wSpin);
        rectGroupForm->addRow(tr("Height (studs):"), hSpin);
        auto* rectWrap = new QWidget(&dlg); rectWrap->setLayout(rectGroupForm);
        form->addRow(tr("Where it goes (a width of 0 keeps its own size):"), rectWrap);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;

        m->sidecar.backgroundImagePath = pathEdit->text();
        m->sidecar.backgroundImageOpacity = opacitySpin->value();
        if (wSpin->value() > 0 && hSpin->value() > 0) {
            m->sidecar.backgroundImageRectStuds = QRectF(
                xSpin->value(), ySpin->value(), wSpin->value(), hSpin->value());
        } else {
            m->sidecar.backgroundImageRectStuds = QRectF();
        }
        mapView_->viewport()->update();
    });
    auto* infoAct = mapMenu->addAction(tr("&General Info…"));
    infoAct->setToolTip(tr("Who made the layout, for which event, and notes about it"));
    generalInfoAct_ = infoAct;
    connect(infoAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Map information"));
        auto* form = new QFormLayout(&dlg);
        auto* authorE = new QLineEdit(m->author, &dlg);
        auto* lugE    = new QLineEdit(m->lug, &dlg);
        auto* eventE  = new QLineEdit(m->event, &dlg);
        auto* dateE   = new QDateEdit(m->date, &dlg); dateE->setCalendarPopup(true);
        auto* commentE = new QPlainTextEdit(m->comment, &dlg);
        commentE->setMinimumHeight(100);
        commentE->setTabChangesFocus(true);  // Tab goes on to OK, as in every other field
        form->addRow(tr("Author:"),  authorE);
        form->addRow(tr("Club (LUG):"), lugE);
        form->addRow(tr("Event:"),   eventE);
        form->addRow(tr("Date:"),    dateE);
        form->addRow(tr("Comment:"), commentE);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        edit::ChangeGeneralInfoCommand::Info next{
            authorE->text(), lugE->text(), eventE->text(),
            dateE->date(), commentE->toPlainText()
        };
        mapView_->undoStack()->push(new edit::ChangeGeneralInfoCommand(*m, std::move(next)));
    });

    mapMenu->addSeparator();
    auto* venueMenu = mapMenu->addMenu(tr("&Venue"));
    auto* designerAct = venueMenu->addAction(tr("Open Venue &Designer…"));
    designerAct->setObjectName(QStringLiteral("map.roomDesigner"));
    designerAct->setToolTip(
        tr("Design this layout's venue: walls, doors, openings, columns, stairs, power, measurements."));
    connect(designerAct, &QAction::triggered, this, [this] {
        auto* m = mapView_->currentMap();
        if (!m) return;
        core::Venue initial = m->sidecar.venue.value_or(edit::venue::emptyVenue(tr("Venue")));
        VenueDesignerDialog dlg(
            std::move(initial), tr("Venue of this layout"), tr("Save to Layout"),
            [this](const core::Venue& v) {
                auto* map = mapView_->currentMap();
                if (!map) return tr("The layout was closed.");
                mapView_->undoStack()->push(new edit::SetVenueCommand(*map, std::make_optional(v)));
                return QString();
            },
            this);
        dlg.setSendToServer([this](const core::Venue& v) { saveVenueToServer(v); });
        dlg.exec();
    });
    // Drawing the outline and obstacles, typing in lengths and the venue's
    // name and walkway are all in the Venue Designer; the older menu items
    // that did each on its own are folded into it (Unclear UI 4).
    auto* clearVenueAct = venueMenu->addAction(tr("&Remove Venue…"));
    clearVenueAct->setObjectName(QStringLiteral("map.removeVenue"));
    clearVenueAct->setToolTip(tr("Take the venue out of this layout (asks first)"));
    connect(clearVenueAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m || !m->sidecar.venue) {
            statusBar()->showMessage(tr("This layout has no venue to remove."), 4000);
            return;
        }
        ConfirmOptions clearVenue;
        clearVenue.title = tr("Remove the venue from this layout?");
        clearVenue.removes = tr("The venue’s outline, walls, doors and obstacles leave this layout.");
        clearVenue.keeps = tr("Your parts stay where they are, and the venue stays in the Venue library if you saved it there.");
        clearVenue.undo = ConfirmDialog::undoWithCtrlZ();
        clearVenue.confirmLabel = tr("Remove");
        if (!ConfirmDialog::ask(this, clearVenue)) return;
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, std::nullopt));
    });

    venueMenu->addSeparator();

    auto* showLibraryAct = venueMenu->addAction(tr("Venue &library…"));
    showLibraryAct->setToolTip(tr("Open the Venue library panel to browse, load, or save venues."));
    connect(showLibraryAct, &QAction::triggered, this, [this]{
        venueLibraryPanel_->show();
        venueLibraryPanel_->raise();
    });

    auto* loadVenueFileAct = venueMenu->addAction(tr("Load Venue from &File…"));
    loadVenueFileAct->setToolTip(tr("Use a room saved as a .bld-venue file"));
    connect(loadVenueFileAct, &QAction::triggered, this, [this]{
        auto* m = mapView_->currentMap();
        if (!m) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Load venue file"), {},
            tr("Venue (*.bld-venue);;All files (*)"));
        if (path.isEmpty()) return;
        QString err;
        auto venue = saveload::readVenueFile(path, &err);
        if (!venue) {
            QMessageBox::warning(this, tr("Load venue"),
                tr("Could not read %1: %2").arg(path, err));
            return;
        }
        if (m->sidecar.venue) {
            const auto btn = QMessageBox::question(this, tr("Replace venue"),
                tr("This project already has a venue. Replace it?"));
            if (btn != QMessageBox::Yes) return;
        }
        mapView_->undoStack()->push(new edit::SetVenueCommand(*m, venue));
    });
}

}  // namespace bld::ui
