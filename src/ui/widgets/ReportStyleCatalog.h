#pragma once

#include <QObject>
#include <QStringList>

#include "ReportDefinition.h"

namespace CardStack {
namespace ReportStyleCatalog {

inline QStringList lineStyleNames()
{
    // Legacy PopulateLineAndFillStyleCombos inserts line-style indexes 0..9.
    // Resource type 263 stores the pen/style table; string 11109 names the final "No Outline" entry.
    QStringList names = {
        QObject::tr("Solid"),
        QObject::tr("Dash"),
        QObject::tr("Dot"),
        QObject::tr("Dash dot"),
        QObject::tr("Dash dot dot"),
        QObject::tr("Thick solid"),
        QObject::tr("Thick dash"),
        QObject::tr("Thick dot"),
        QObject::tr("Hairline"),
        QObject::tr("No Outline"),
    };
    Q_ASSERT(names.size() == ReportLineStyleCount);
    return names;
}

inline QStringList fillPatternNames()
{
    // Legacy PopulateLineAndFillStyleCombos inserts fill-pattern indexes 0..25.
    // These names come directly from legacy strings 11000..11025.
    QStringList names = {
        QObject::tr("Clear"),
        QObject::tr("Solid"),
        QObject::tr("5%"),
        QObject::tr("10%"),
        QObject::tr("20%"),
        QObject::tr("25%"),
        QObject::tr("30%"),
        QObject::tr("40%"),
        QObject::tr("50%"),
        QObject::tr("60%"),
        QObject::tr("70%"),
        QObject::tr("75%"),
        QObject::tr("80%"),
        QObject::tr("90%"),
        QObject::tr("Dk Horizontal"),
        QObject::tr("Dk Vertical"),
        QObject::tr("Dk Dwn Diagonal"),
        QObject::tr("Dk Up Diagonal"),
        QObject::tr("Dk Grid"),
        QObject::tr("Dk Trellis"),
        QObject::tr("Lt Horizontal"),
        QObject::tr("Lt Vertical"),
        QObject::tr("Lt Dwn Diagonal"),
        QObject::tr("Lt Up Diagonal"),
        QObject::tr("Lt Grid"),
        QObject::tr("Lt Trellis"),
    };
    Q_ASSERT(names.size() == ReportFillPatternCount);
    return names;
}

} // namespace ReportStyleCatalog
} // namespace CardStack
