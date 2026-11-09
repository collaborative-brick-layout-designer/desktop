#pragma once

// ctest runs each test in a process of its own, several side by side with
// -j. Each process keeps its app data and settings apart, under an app name
// of its own (with its process id), in both of QStandardPaths' stores (test
// mode on and off), and removes them when it ends. They used to share one:
// under `ctest -j4`, ImportReplace and AddPartsFromZip failed when a test
// beside them changed the parts folders or the settings.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

class ProcessStores {
public:
    explicit ProcessStores(const QString& app) {
        QCoreApplication::setOrganizationName(QStringLiteral("BrickLayoutDesignerTests"));
        QCoreApplication::setApplicationName(app + QLatin1Char('-') + QString::number(QCoreApplication::applicationPid()));
    }
    ~ProcessStores() {
        const bool was = QStandardPaths::isTestModeEnabled();
        for (const bool testMode : { false, true }) {
            QStandardPaths::setTestModeEnabled(testMode);
            {
                QSettings s;
                s.clear();
                s.sync();
                QFile::remove(s.fileName());
            }
            for (const auto where : { QStandardPaths::AppDataLocation, QStandardPaths::AppConfigLocation,
                                      QStandardPaths::CacheLocation }) {
                const QString dir = QStandardPaths::writableLocation(where);
                // Only this process's own folder (its name has the process id).
                if (dir.contains(QCoreApplication::applicationName())) QDir(dir).removeRecursively();
            }
        }
        QStandardPaths::setTestModeEnabled(was);
    }
};
