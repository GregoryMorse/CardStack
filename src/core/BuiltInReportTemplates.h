#pragma once

#include "ReportDefinition.h"

#include <QString>
#include <QVector>

namespace CardStack {

class Deck;

QVector<ReportDefinition> builtInReportTemplatesForLegacyResource(int legacyResourceId);
QVector<ReportDefinition> standardReportDefinitionsForDeck(
    const Deck& deck,
    const QString& pageReportName,
    const QString& rowReportName);

} // namespace CardStack
