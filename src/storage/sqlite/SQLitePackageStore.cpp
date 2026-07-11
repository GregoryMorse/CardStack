#include "SQLitePackageStore.h"

#include "SQLiteDeckStore.h"

namespace CardStack {
namespace {

constexpr auto kPackageTypeSetting = "package_type";

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

Deck makeTemplateOnlyDeck(const Deck& sourceDeck)
{
    Deck templateDeck(sourceDeck.name().isEmpty() ? QStringLiteral("Untitled Template") : sourceDeck.name());
    templateDeck.setDescription(sourceDeck.description());

    for (int index = 0; index < sourceDeck.fieldCount(); ++index) {
        templateDeck.addField(sourceDeck.fieldAt(index));
    }

    templateDeck.setSortKeys(sourceDeck.sortKeys());
    templateDeck.setImportExportProfiles(sourceDeck.importExportProfiles());
    templateDeck.setCardTemplateLayout(sourceDeck.cardTemplateLayout());

    for (int index = 0; index < sourceDeck.reportCount(); ++index) {
        templateDeck.addReport(sourceDeck.reportAt(index));
    }

    return templateDeck;
}

Deck makeReportPackageDeck(const QVector<ReportDefinition>& reports, const QString& packageName)
{
    Deck reportDeck(packageName.isEmpty() ? QStringLiteral("Shared Reports") : packageName);
    reportDeck.setDescription(QStringLiteral("CardStack report package"));

    for (const ReportDefinition& report : reports) {
        reportDeck.addReport(report);
    }

    return reportDeck;
}

bool savePackageDeck(const Deck& deck, SQLitePackageType packageType, const QString& filePath, QString* errorMessage)
{
    SQLiteDeckStore store;
    if (!store.open(filePath, errorMessage)
        || !store.saveDeck(deck, errorMessage)
        || !store.setAppSetting(QString::fromLatin1(kPackageTypeSetting), sqlitePackageTypeName(packageType), errorMessage)
        || !store.setAppSetting(QStringLiteral("package_version"), QStringLiteral("1"), errorMessage)) {
        return false;
    }

    return true;
}

} // namespace

QString sqlitePackageTypeName(SQLitePackageType type)
{
    switch (type) {
    case SQLitePackageType::Deck:
        return QStringLiteral("deck");
    case SQLitePackageType::Template:
        return QStringLiteral("template");
    case SQLitePackageType::Report:
        return QStringLiteral("report");
    case SQLitePackageType::Unknown:
        break;
    }

    return QStringLiteral("unknown");
}

SQLitePackageType sqlitePackageTypeFromName(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("deck")) {
        return SQLitePackageType::Deck;
    }
    if (normalized == QLatin1String("template")) {
        return SQLitePackageType::Template;
    }
    if (normalized == QLatin1String("report")) {
        return SQLitePackageType::Report;
    }

    return SQLitePackageType::Unknown;
}

bool SQLitePackageStore::saveDeckPackage(const Deck& deck, const QString& filePath, QString* errorMessage)
{
    return savePackageDeck(deck, SQLitePackageType::Deck, filePath, errorMessage);
}

bool SQLitePackageStore::saveTemplatePackage(const Deck& sourceDeck, const QString& filePath, QString* errorMessage)
{
    return savePackageDeck(makeTemplateOnlyDeck(sourceDeck), SQLitePackageType::Template, filePath, errorMessage);
}

bool SQLitePackageStore::loadTemplatePackage(const QString& filePath, Deck* templateDeck, QString* errorMessage)
{
    if (templateDeck == nullptr) {
        setError(errorMessage, QStringLiteral("Template output is null."));
        return false;
    }

    SQLiteDeckStore store;
    if (!store.open(filePath, errorMessage)) {
        return false;
    }

    QString packageTypeValue;
    if (!store.appSetting(QString::fromLatin1(kPackageTypeSetting), &packageTypeValue, errorMessage)) {
        return false;
    }

    const SQLitePackageType type = sqlitePackageTypeFromName(packageTypeValue);
    if (type == SQLitePackageType::Report) {
        setError(errorMessage, QStringLiteral("Report packages cannot be loaded as card templates."));
        return false;
    }
    if (type != SQLitePackageType::Template) {
        setError(errorMessage, QStringLiteral("Only CardStack template packages can be loaded as card templates."));
        return false;
    }

    Deck loaded;
    if (!store.loadDeck(&loaded, errorMessage)) {
        return false;
    }

    *templateDeck = makeTemplateOnlyDeck(loaded);
    return true;
}

bool SQLitePackageStore::saveReportPackage(const QVector<ReportDefinition>& reports,
                                           const QString& packageName,
                                           const QString& filePath,
                                           QString* errorMessage)
{
    if (reports.isEmpty()) {
        setError(errorMessage, QStringLiteral("Report package must contain at least one report."));
        return false;
    }

    return savePackageDeck(makeReportPackageDeck(reports, packageName), SQLitePackageType::Report, filePath, errorMessage);
}

bool SQLitePackageStore::loadReportPackage(const QString& filePath,
                                           QVector<ReportDefinition>* reports,
                                           QString* packageName,
                                           QString* errorMessage)
{
    if (reports == nullptr) {
        setError(errorMessage, QStringLiteral("Report output is null."));
        return false;
    }

    SQLiteDeckStore store;
    if (!store.open(filePath, errorMessage)) {
        return false;
    }

    QString packageTypeValue;
    if (!store.appSetting(QString::fromLatin1(kPackageTypeSetting), &packageTypeValue, errorMessage)) {
        return false;
    }

    const SQLitePackageType type = sqlitePackageTypeFromName(packageTypeValue);
    if (type == SQLitePackageType::Template) {
        setError(errorMessage, QStringLiteral("Template packages cannot be loaded as report packages."));
        return false;
    }
    if (type != SQLitePackageType::Report) {
        setError(errorMessage, QStringLiteral("Only CardStack report packages can be loaded as report packages."));
        return false;
    }

    Deck loaded;
    if (!store.loadDeck(&loaded, errorMessage)) {
        return false;
    }

    QVector<ReportDefinition> loadedReports;
    for (int index = 0; index < loaded.reportCount(); ++index) {
        loadedReports.append(loaded.reportAt(index));
    }

    if (loadedReports.isEmpty()) {
        setError(errorMessage, QStringLiteral("Package does not contain any reports."));
        return false;
    }

    *reports = loadedReports;
    if (packageName != nullptr) {
        *packageName = loaded.name();
    }

    return true;
}

bool SQLitePackageStore::packageType(const QString& filePath, SQLitePackageType* type, QString* errorMessage)
{
    if (type == nullptr) {
        setError(errorMessage, QStringLiteral("Package type output is null."));
        return false;
    }

    SQLiteDeckStore store;
    if (!store.open(filePath, errorMessage)) {
        return false;
    }

    QString packageTypeValue;
    if (!store.appSetting(QString::fromLatin1(kPackageTypeSetting), &packageTypeValue, errorMessage)) {
        return false;
    }

    *type = sqlitePackageTypeFromName(packageTypeValue);
    return true;
}

} // namespace CardStack
