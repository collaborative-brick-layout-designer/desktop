#pragma once

// Answers the modal dialogs a step of a UI test opens (message boxes, file
// dialogs, any QDialog), in order: shared by the main-window file tests.

#include "ui/MainWindow.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <vector>

namespace {

// Answers the modal dialogs that open while `run` runs, in order; a dialog
// beyond the answers is closed and counted as unexpected. A dialog answered
// may close later (a queued accept()): the next step waits until it's gone,
// so it is never taken for the next dialog. One still open 5 s after its
// answer fails the test and is closed.
struct Answers {
    std::vector<std::function<void(QWidget*)>> steps;
    int unexpected = 0;
    void during(const std::function<void()>& run) {
        std::size_t next = 0;
        QPointer<QWidget> answered;
        QElapsedTimer sinceAnswer;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, [&] {
            if (answered && answered->isVisible()) {
                if (sinceAnswer.elapsed() < 5000) return;
                ADD_FAILURE() << "a dialog stayed open after its answer";
                answered->close();
            }
            answered = nullptr;
            QWidget* modal = QApplication::activeModalWidget();
            if (!modal || !modal->isVisible()) return;
            answered = modal;
            sinceAnswer.start();
            if (next < steps.size()) steps[next++](modal);
            else {
                ++unexpected;
                modal->close();
            }
        });
        poll.start();
        run();
        poll.stop();
        EXPECT_EQ(next, steps.size()) << "fewer dialogs than expected";
    }
};

inline std::function<void(QWidget*)> click(const QString& text) {
    return [text](QWidget* w) {
        auto* box = qobject_cast<QMessageBox*>(w);
        ASSERT_NE(box, nullptr);
        for (QAbstractButton* b : box->buttons()) {
            if (QString(b->text()).remove(QLatin1Char('&')) == text) {
                b->click();
                return;
            }
        }
        ADD_FAILURE() << "no button " << text.toStdString();
        box->close();
    };
}

// Clicks the button named `text` in any dialog (not only a message box).
inline std::function<void(QWidget*)> clickIn(const QString& text,
                                             const std::function<void(QWidget*)>& first = {}) {
    return [text, first](QWidget* w) {
        if (first) first(w);
        for (QAbstractButton* b : w->findChildren<QAbstractButton*>()) {
            if (b->isVisible() && QString(b->text()).remove(QLatin1Char('&')) == text) {
                b->click();
                return;
            }
        }
        ADD_FAILURE() << "no button " << text.toStdString();
        w->close();
    };
}

inline std::function<void(QWidget*)> chooseFile(const QString& path) {
    return [path](QWidget* w) {
        auto* dialog = qobject_cast<QFileDialog*>(w);
        ASSERT_NE(dialog, nullptr);
        // selectFile() can lose to the directory model still loading;
        // the name box is what accept() reads.
        dialog->setDirectory(QFileInfo(path).absolutePath());
        auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        ASSERT_NE(name, nullptr);
        name->setText(QFileInfo(path).fileName());
        QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
    };
}

inline bool invoke(bld::ui::MainWindow& w, const char* slot) {
    bool result = false;
    QMetaObject::invokeMethod(&w, slot, Qt::DirectConnection, qReturnArg(result));
    return result;
}

} // namespace
