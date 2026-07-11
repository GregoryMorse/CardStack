#pragma once

#include <QString>
#include <QVector>

namespace CardStack {

enum class ImportExportProfileType {
    Import,
    Export
};

enum class DelimitedTextFormat {
    Csv,
    Tsv
};

struct ImportExportProfile {
    QString name;
    ImportExportProfileType type = ImportExportProfileType::Export;
    DelimitedTextFormat format = DelimitedTextFormat::Csv;
    QChar delimiter = QLatin1Char(',');
    bool hasHeader = true;
    QVector<int> fieldMappings;
};

} // namespace CardStack
