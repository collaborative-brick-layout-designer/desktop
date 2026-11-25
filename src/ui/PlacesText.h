#pragma once

// What places are called (Live Ops design 6): the web's editor/places.ts says the same.

#include <QCoreApplication>
#include <QJsonArray>
#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace bld::ui {

class PlacesText {
    Q_DECLARE_TR_FUNCTIONS(bld::ui::PlacesText)
public:
    static QString kind(const QString& kind);
    static QString kindHint(const QString& kind);
    static QString role(const QString& role);
    // "2 industries, 1 yard".
    static QString counts(const QList<std::pair<QString, int>>& counts);
    static QString places(const QJsonArray& places);
    // "3 cars", "Too short for a car"; empty when it can't be told.
    static QString capacity(std::optional<int> cars);
    static QString capacityOf(int cars);
};

}  // namespace bld::ui
