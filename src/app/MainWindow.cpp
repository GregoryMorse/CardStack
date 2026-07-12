#include "MainWindow.h"

#include "BuiltInReportTemplates.h"
#include "DelimitedText.h"
#include "DeckTemplate.h"
#include "LegacyDeckReader.h"
#include "LegacyInterchangeReader.h"
#include "LegacyReportReader.h"
#include "DeckWorkspace.h"
#include "UiIds.h"
#include "ReportDesignerWidget.h"
#include "ReportPreviewDialog.h"
#include "ReportPrintEngine.h"
#include "ReportPreviewRenderer.h"
#include "ReportStyleCatalog.h"
#include "UiBuilder.h"
#include "SQLiteDeckStore.h"
#include "SQLitePackageStore.h"
#include "TemplateDesignerWidget.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEventLoop>
#include <QFontDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QLineEdit>
#include <QMarginsF>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSetupDialog>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRect>
#include <QRegularExpression>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTime>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

extern int qInitResources_cardstack_app();

namespace {

void initializeCardStackApplicationResources()
{
    ::qInitResources_cardstack_app();
}

} // namespace

namespace CardStack {

namespace {

namespace Command = UiIds::Command;
namespace Control = UiIds::Control;
namespace Menu = UiIds::Menu;

constexpr int StatusMessageTimeoutMs = 5000;

QIcon cardStackIcon()
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-24.png"), QSize(24, 24));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-64.png"), QSize(64, 64));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-128.png"), QSize(128, 128));
    icon.addFile(QStringLiteral(":/cardstack/icons/icon-256.png"), QSize(256, 256));
    return icon;
}

bool fieldNameLooksLikePhone(const QString& fieldName)
{
    const QString normalized = fieldName.toLower();
    return normalized.contains(QStringLiteral("phone")) ||
        normalized.contains(QStringLiteral("mobile")) ||
        normalized.contains(QStringLiteral("cell")) ||
        normalized.contains(QStringLiteral("fax")) ||
        normalized.contains(QStringLiteral("tel"));
}

bool valueLooksLikePhoneNumber(const QString& value)
{
    static const QRegularExpression phoneCharacters(QStringLiteral("[0-9][0-9\\s().+\\-#/]{4,}"));
    return phoneCharacters.match(value).hasMatch();
}

QString quickDialDisplay(const QString& description, const QString& phoneNumber)
{
    return description.trimmed().isEmpty()
        ? phoneNumber.trimmed()
        : QStringLiteral("%1: %2").arg(description.trimmed(), phoneNumber.trimmed());
}

QString normalizedPhoneDialString(QString value)
{
    QString normalized;
    normalized.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isDigit() || ch == QLatin1Char('+') || ch == QLatin1Char('#') || ch == QLatin1Char('*')) {
            normalized.append(ch);
        }
    }
    return normalized;
}

QStringList phoneCandidatesForWorkspace(const DeckWorkspace& workspace)
{
    const Deck& deck = workspace.deck();
    const int cardIndex = workspace.currentCardIndex();
    if (cardIndex < 0 || cardIndex >= deck.cardCount()) {
        return {};
    }

    const CardRecord& record = deck.cardAt(cardIndex);
    QStringList candidates;
    for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
        const QString value = record.valueAt(fieldIndex).trimmed();
        if (value.isEmpty()) {
            continue;
        }
        const QString fieldName = deck.fieldAt(fieldIndex).name();
        if (fieldNameLooksLikePhone(fieldName) || valueLooksLikePhoneNumber(value)) {
            const QString candidate = fieldName.isEmpty()
                ? value
                : QStringLiteral("%1: %2").arg(fieldName, value);
            candidates.append(candidate);
        }
    }
    candidates.removeDuplicates();
    return candidates;
}

QString phoneValueFromCandidate(const QString& candidate)
{
    const int separator = candidate.indexOf(QStringLiteral(": "));
    return separator >= 0 ? candidate.mid(separator + 2).trimmed() : candidate.trimmed();
}

enum class PrintCardScope {
    ThisCard,
    AllCards,
    SelectedCards
};

constexpr int DefaultReportPageWidthMils = 8500;
constexpr int DefaultReportPageHeightMils = 11000;
constexpr int DefaultReportMarginMils = 500;
constexpr int DefaultCardFormWidthMils = 5000;
constexpr int DefaultCardFormHeightMils = 3000;
constexpr int DefaultLabelFormWidthMils = 2625;
constexpr int DefaultLabelFormHeightMils = 1000;
constexpr int DefaultLabelRows = 10;
constexpr int DefaultLabelColumns = 3;
constexpr int DefineFormMeasureEditWidthPx = 48;
constexpr int DefineFormCountEditWidthPx = 36;
constexpr int MainToolbarIconSizePx = 24;
constexpr int IndexBarButtonMinHeightPx = 22;
constexpr int IndexBarButtonMaxHeightPx = 24;
constexpr int IndexBarHorizontalMarginPx = 2;
constexpr int IndexBarVerticalMarginPx = 1;
constexpr int IndexBarButtonGapPx = 1;
constexpr int ReportFormListMinimumHeightPx = 116;
constexpr int ReportFormDialogBottomPaddingPx = 54;
constexpr int ReportDesignerWindowWidthPx = 920;
constexpr int ReportDesignerWindowHeightPx = 620;
constexpr int TemplateDesignerWindowWidthPx = 940;
constexpr int TemplateDesignerWindowHeightPx = 620;
constexpr int HtmlDialogWidthPx = 760;
constexpr int HtmlDialogHeightPx = 620;

struct ReportFormPreset {
    ReportFormType type = ReportFormType::Report;
    const char* label = "";
    int formWidth = DefaultReportPageWidthMils;
    int formHeight = DefaultReportPageHeightMils;
    int rows = 1;
    int columns = 1;
    int marginLeft = DefaultReportMarginMils;
    int marginTop = DefaultReportMarginMils;
    int marginRight = DefaultReportMarginMils;
    int marginBottom = DefaultReportMarginMils;
    int horizontalGutter = 0;
    int verticalGutter = 0;
};

QVector<ReportFormPreset> reportFormPresets(ReportFormType type)
{
    if (type == ReportFormType::Card) {
        return {
            {ReportFormType::Card, QT_TR_NOOP("Index 5 x 3 (pin)"), 5000, 3000, 1, 1, 0, 0, 0, 0, 0, 0},
            {ReportFormType::Card, QT_TR_NOOP("Post Card 6 x 4 (laser)"), 6000, 4000, 2, 1, 1500, 1250, 1500, 1250, 0, 0},
            {ReportFormType::Card, QT_TR_NOOP("Envelope #10 9½ x 4⅛ (laser)"), 9500, 4125, 1, 1, 0, 0, 0, 0, 0, 0},
            {ReportFormType::Card, QT_TR_NOOP("Rotary Index 5 x 3 (laser)"), 5000, 3000, 3, 1, 1000, 1750, 1000, 1750, 0, 0},
        };
    }

    if (type == ReportFormType::Label) {
        return {
            {ReportFormType::Label, QT_TR_NOOP("Address 2⅝ x 1 3-across (laser)"), 2833, 1000, 10, 3, 500, 500, 500, 500, 0, 0},
            {ReportFormType::Label, QT_TR_NOOP("Address 4¼ x 1⅓ 2-across (laser)"), 4250, 1333, 7, 2, 834, 835, 834, 835, 0, 0},
            {ReportFormType::Label, QT_TR_NOOP("Shipping 4 x 2 2-across (laser)"), 4000, 2000, 5, 2, 500, 500, 500, 500, 188, 0},
            {ReportFormType::Label, QT_TR_NOOP("File Folder 3 7⁄16 x ⅔ 2-across (laser)"), 3438, 667, 15, 2, 495, 500, 495, 500, 562, 0},
            {ReportFormType::Label, QT_TR_NOOP("Address 3 x 15⁄16 (pin)"), 3000, 938, 1, 1, 62, 500, 62, 500, 0, 0},
        };
    }

    return {
        {ReportFormType::Report, QT_TR_NOOP("Letter (portrait)\t8½ x 11"), 8500, 11000, 1, 1, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, 0, 0},
        {ReportFormType::Report, QT_TR_NOOP("Legal\t8½ x 14"), 8500, 14000, 1, 1, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, 0, 0},
        {ReportFormType::Report, QT_TR_NOOP("Note (portrait)\t5½ x 8½"), 5500, 8500, 1, 1, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, 0, 0},
        {ReportFormType::Report, QT_TR_NOOP("Letter (landscape)\t11 x 8½"), 11000, 8500, 1, 1, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, 0, 0},
        {ReportFormType::Report, QT_TR_NOOP("Note (landscape)\t8½ x 5½"), 8500, 5500, 1, 1, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, DefaultReportMarginMils, 0, 0},
    };
}

void applyReportFormPreset(ReportDefinition* report, const ReportFormPreset& preset)
{
    if (report == nullptr) {
        return;
    }

    report->formType = preset.type;
    report->formWidth = preset.formWidth;
    report->formHeight = preset.formHeight;
    report->rows = preset.rows;
    report->columns = preset.columns;
    report->marginLeft = preset.marginLeft;
    report->marginTop = preset.marginTop;
    report->marginRight = preset.marginRight;
    report->marginBottom = preset.marginBottom;
    report->horizontalGutter = preset.horizontalGutter;
    report->verticalGutter = preset.verticalGutter;
}

bool isDeckCommand(int commandId)
{
    return commandId == Command::FileRedefine ||
        commandId == Command::FileMerge ||
        commandId == Command::FilePrintReport ||
        commandId == Command::FileNewReport ||
        (commandId >= Command::EditFirst && commandId <= Command::EditLast) ||
        (commandId >= Command::CardFirst && commandId <= Command::CardLast) ||
        commandId == Command::ViewCard || commandId == Command::ViewTable ||
        commandId == Command::SearchFind || commandId == Command::SearchFindNext ||
        commandId == Command::SearchReplace ||
        (commandId >= Command::ConfigureFontFirst && commandId <= Command::ConfigureAppearanceLast) ||
        commandId == Command::ConfigureIndex ||
        commandId == Command::NavigateFirstCard || commandId == Command::NavigateLastCard ||
        (commandId >= Command::NavigateFirst && commandId <= Command::NavigateLast);
}

bool isReportDesignerCommand(int commandId)
{
    return commandId == Command::FileNewReport ||
        commandId == Command::FileOpenReport ||
        commandId == Command::FileCloseDeck ||
        commandId == Command::FileClose ||
        commandId == Command::FileSaveAs ||
        commandId == Command::FileSaveReport ||
        commandId == Command::FileSaveReportAs ||
        commandId == Command::EditUndo ||
        commandId == Command::EditCut ||
        commandId == Command::EditCopy ||
        commandId == Command::EditPaste ||
        commandId == Command::EditClear ||
        commandId == Command::ToolAddText ||
        commandId == Command::ToolAddDataBox ||
        commandId == Command::ToolAddSystemData ||
        commandId == Command::ToolAddLineOrBox ||
        commandId == Command::ToolFrameAttributes ||
        commandId == Command::ToolChangeForm ||
        commandId == Command::ConfigureDataFont ||
        commandId == Command::ConfigureTextFont ||
        commandId == Command::ConfigureColors;
}

bool isTemplateDesignerCommand(int commandId)
{
    return commandId == Command::FileSave ||
        commandId == Command::FileSaveAs ||
        commandId == Command::FileClose ||
        commandId == Command::EditClear ||
        commandId == Command::ToolAddText ||
        commandId == Command::ToolAddDataBox ||
        commandId == Command::ToolAddNotesBox ||
        commandId == Command::ToolAddLineOrBox ||
        commandId == Command::ToolFrameAttributes ||
        commandId == Command::ConfigureDataFont ||
        commandId == Command::ConfigureNameFont ||
        commandId == Command::ConfigureTextFont ||
        commandId == Command::ConfigureIndexFont ||
        commandId == Command::ConfigureColors;
}

QStringList defaultTemplates()
{
    return builtInDeckTemplateNames();
}

QStringList defaultReports()
{
    return {
        QObject::tr("Default Page Report"),
        QObject::tr("Default Row Report"),
    };
}

template <typename T>
T* uiControl(const QDialog& dialog, int controlId)
{
    return qobject_cast<T*>(UiBuilder::controlById(const_cast<QDialog*>(&dialog), controlId));
}

QString comboText(const QDialog& dialog, int controlId)
{
    if (auto* comboBox = uiControl<QComboBox>(dialog, controlId)) {
        return comboBox->currentText();
    }
    return {};
}

QString editText(const QDialog& dialog, int controlId)
{
    if (auto* lineEdit = uiControl<QLineEdit>(dialog, controlId)) {
        return lineEdit->text();
    }
    return {};
}

void setEditText(QDialog& dialog, int controlId, const QString& text)
{
    if (auto* lineEdit = uiControl<QLineEdit>(dialog, controlId)) {
        lineEdit->setText(text);
    }
}

void setChecked(QDialog& dialog, int controlId, bool checked)
{
    if (auto* button = uiControl<QAbstractButton>(dialog, controlId)) {
        button->setChecked(checked);
    }
}

void setLabelText(QDialog& dialog, int controlId, const QString& text)
{
    if (auto* label = uiControl<QLabel>(dialog, controlId)) {
        label->setText(text);
    }
}

int positiveEditValue(const QDialog& dialog, int controlId, int fallback)
{
    bool ok = false;
    const int value = editText(dialog, controlId).trimmed().toInt(&ok);
    return ok && value > 0 ? value : fallback;
}

int formMeasureValue(const QDialog& dialog, int controlId, int fallback)
{
    bool ok = false;
    const double value = editText(dialog, controlId).trimmed().toDouble(&ok);
    if (!ok || value <= 0.0) {
        return fallback;
    }
    if (value <= 100.0) {
        return static_cast<int>(value * 1000.0 + 0.5);
    }
    return static_cast<int>(value + 0.5);
}

int comboIndex(const QDialog& dialog, int controlId)
{
    if (auto* comboBox = uiControl<QComboBox>(dialog, controlId)) {
        return comboBox->currentIndex();
    }
    return 0;
}

QStringList legacyLineStyleNames()
{
    return ReportStyleCatalog::lineStyleNames();
}

QStringList legacyFillPatternNames()
{
    return ReportStyleCatalog::fillPatternNames();
}

void populateComboIfEmpty(QDialog& dialog, int controlId, const QStringList& values)
{
    if (auto* comboBox = uiControl<QComboBox>(dialog, controlId); comboBox != nullptr && comboBox->count() == 0) {
        comboBox->addItems(values);
    }
}

void initializeLineFrameDialog(QDialog& dialog)
{
    populateComboIfEmpty(dialog, Control::LineFrameLineStyle, legacyLineStyleNames());
    populateComboIfEmpty(dialog, Control::LineFrameFillPattern, legacyFillPatternNames());
    setEditText(dialog, Control::LineFrameCornerRadius, QStringLiteral("0"));
}

void initializeSystemBoxDialog(QDialog& dialog)
{
    populateComboIfEmpty(
        dialog,
        Control::SystemBoxDateFormats,
        {
            QObject::tr("Date - long"),
            QObject::tr("Date - short"),
            QObject::tr("Day - long"),
            QObject::tr("Day - short"),
            QObject::tr("Weekday - long"),
            QObject::tr("Weekday - short"),
            QObject::tr("Month - long"),
            QObject::tr("Month - short"),
            QObject::tr("Year - long"),
            QObject::tr("Year - short"),
            QObject::tr("Time - long"),
            QObject::tr("Time - short"),
            QObject::tr("Hour"),
            QObject::tr("Minutes"),
            QObject::tr("AM/PM"),
        });
    populateComboIfEmpty(
        dialog,
        Control::SystemBoxNumberFormats,
        {QObject::tr("Page number"), QObject::tr("Cards in deck")});
    populateComboIfEmpty(
        dialog,
        Control::SystemBoxSystemFields,
        {
            QObject::tr("Report name"),
            QObject::tr("Deck name"),
            QObject::tr("Deck description"),
            QObject::tr("Deck file path"),
        });
    const auto checked = [&dialog](int controlId) {
        const auto* button = uiControl<QAbstractButton>(dialog, controlId);
        return button != nullptr && button->isChecked();
    };
    if (!checked(Control::SystemBoxDateCategory)
        && !checked(Control::SystemBoxNumberCategory)
        && !checked(Control::SystemBoxSystemCategory)) {
        setChecked(dialog, Control::SystemBoxDateCategory, true);
    }
}

bool isChecked(const QDialog& dialog, int controlId)
{
    const auto* button = uiControl<QAbstractButton>(dialog, controlId);
    return button != nullptr && button->isChecked();
}

bool reportNameExists(const DeckWorkspace* workspace, const QString& reportName, int exceptReportIndex)
{
    if (workspace == nullptr) {
        return false;
    }

    const QString normalizedName = reportName.trimmed();
    if (normalizedName.isEmpty()) {
        return false;
    }

    const Deck& deck = workspace->deck();
    for (int index = 0; index < deck.reportCount(); ++index) {
        if (index != exceptReportIndex
            && deck.reportAt(index).name.compare(normalizedName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

quint8 frameStyleFlagsFromDialog(const QDialog& dialog)
{
    quint8 flags = 0;
    if (isChecked(dialog, Control::FrameBold) || isChecked(dialog, Control::SystemBoxBold)) {
        flags |= ReportStyleFlagBold;
    }
    if (isChecked(dialog, Control::FrameItalic) || isChecked(dialog, Control::SystemBoxItalic)) {
        flags |= ReportStyleFlagItalic;
    }
    if (isChecked(dialog, Control::FrameUnderline) || isChecked(dialog, Control::SystemBoxUnderline)) {
        flags |= ReportStyleFlagUnderline;
    }
    if (isChecked(dialog, Control::FrameAlignmentRight) || isChecked(dialog, Control::SystemBoxRight)) {
        flags |= ReportStyleFlagAlignRight;
    } else if (isChecked(dialog, Control::FrameAlignmentCenter) || isChecked(dialog, Control::SystemBoxCenter)) {
        flags |= ReportStyleFlagAlignCenter;
    }
    return flags;
}

int fieldIndexFromCombo(const QDialog& dialog, int controlId)
{
    const int comboIndexValue = comboIndex(dialog, controlId);
    return comboIndexValue <= 0 ? -1 : comboIndexValue - 1;
}

DeckWorkspace::SearchType searchTypeFromCombo(const QDialog& dialog, int controlId)
{
    return static_cast<DeckWorkspace::SearchType>(
        std::clamp(comboIndex(dialog, controlId), 0, 7));
}

bool isCardStackDeckPath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("cardstack") || suffix == QStringLiteral("csdeck");
}

bool isDelimitedTextPath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("csv") || suffix == QStringLiteral("tsv") || suffix == QStringLiteral("tab");
}

bool isCardStackTemplatePath(const QString& filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("cstemplate"), Qt::CaseInsensitive) == 0;
}

bool isCardStackReportPath(const QString& filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("csreport"), Qt::CaseInsensitive) == 0;
}

bool isLegacyReportPath(const QString& filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("rpt"), Qt::CaseInsensitive) == 0;
}

ImportExportProfile delimitedTextProfileForPath(const QString& filePath, ImportExportProfileType type)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("tsv") || suffix == QStringLiteral("tab")
        ? DelimitedText::tsvProfile(type)
        : DelimitedText::csvProfile(type);
}

QString pathWithDefaultDeckSuffix(QString filePath)
{
    if (!filePath.isEmpty() && QFileInfo(filePath).suffix().isEmpty()) {
        filePath.append(QStringLiteral(".cardstack"));
    }
    return filePath;
}

QString pathWithDefaultTemplateSuffix(QString filePath)
{
    if (!filePath.isEmpty() && QFileInfo(filePath).suffix().isEmpty()) {
        filePath.append(QStringLiteral(".cstemplate"));
    }
    return filePath;
}

QString pathWithDefaultReportSuffix(QString filePath)
{
    if (!filePath.isEmpty() && QFileInfo(filePath).suffix().isEmpty()) {
        filePath.append(QStringLiteral(".csreport"));
    }
    return filePath;
}

QString legacyReportSidecarPath(const QString& deckPath)
{
    const QFileInfo deckInfo(deckPath);
    const QString basePath = deckInfo.dir().filePath(deckInfo.completeBaseName());
    const QStringList suffixes = {
        QStringLiteral(".RPT"),
        QStringLiteral(".rpt"),
    };

    for (const QString& suffix : suffixes) {
        const QString path = basePath + suffix;
        if (QFileInfo::exists(path)) {
            return path;
        }
    }

    return {};
}

QStringList reportNamesFromDeck(const Deck& deck)
{
    QStringList names;
    for (const ReportDefinition& report : deck.reports()) {
        names.append(report.name);
    }
    return names;
}

QStringList fieldNamesFromDeck(const Deck& deck)
{
    QStringList names;
    for (const FieldDefinition& field : deck.fields()) {
        names.append(field.name());
    }
    return names;
}

QString mergeDestinationText(const Deck& destination, const Deck& source, int destinationFieldIndex, int sourceFieldIndex)
{
    const QString destinationName = destination.fieldAt(destinationFieldIndex).name();
    if (sourceFieldIndex < 0 || sourceFieldIndex >= source.fieldCount()) {
        return destinationName;
    }
    return QStringLiteral("%1  <-  %2").arg(destinationName, source.fieldAt(sourceFieldIndex).name());
}

int bestSourceFieldForDestination(const Deck& source, const Deck& destination, int destinationFieldIndex)
{
    const QString destinationName = destination.fieldAt(destinationFieldIndex).name();
    for (int sourceIndex = 0; sourceIndex < source.fieldCount(); ++sourceIndex) {
        if (source.fieldAt(sourceIndex).name().compare(destinationName, Qt::CaseInsensitive) == 0) {
            return sourceIndex;
        }
    }
    return destinationFieldIndex < source.fieldCount() ? destinationFieldIndex : -1;
}

ReportDefinition createDefaultReportDefinition(const Deck& deck, QString name = {})
{
    ReportDefinition report;
    report.name = name.trimmed().isEmpty() ? QObject::tr("Untitled Report") : std::move(name);
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.formType = ReportFormType::Report;
    report.formWidth = DefaultReportPageWidthMils;
    report.formHeight = DefaultReportPageHeightMils;
    report.rows = 1;
    report.columns = 1;
    report.marginLeft = DefaultReportMarginMils;
    report.marginTop = DefaultReportMarginMils;
    report.marginRight = DefaultReportMarginMils;
    report.marginBottom = DefaultReportMarginMils;
    report.textFont.faceName = QStringLiteral("Arial");
    report.dataFont.faceName = QStringLiteral("Arial");

    ReportFrameDefinition title;
    title.signature = 0xabcd;
    title.kind = ReportFrameKind::SystemText;
    title.bounds = QRect(500, 350, 3600, 360);
    title.text = QStringLiteral("{reportname}");
    title.systemTokens = {QStringLiteral("reportname")};
    report.frames.append(title);

    if (deck.fieldCount() > 0) {
        ReportFrameDefinition data;
        data.signature = 0xabcd;
        data.kind = ReportFrameKind::Data;
        data.bounds = QRect(500, 950, 3600, 360);
        const QString fieldName = deck.fieldAt(0).name();
        data.text = QStringLiteral("[%1]").arg(fieldName);
        data.fieldPlaceholders = {fieldName};
        report.frames.append(data);
    }

    return report;
}

int addStandardReportDefinitions(DeckWorkspace* workspace)
{
    if (workspace == nullptr) {
        return 0;
    }

    int added = 0;
    const auto hasReportNamed = [workspace](const QString& name) {
        for (const ReportDefinition& report : workspace->deck().reports()) {
            if (report.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };

    const QString pageReportName = QObject::tr("Default Page Report");
    const QString rowReportName = QObject::tr("Default Row Report");
    const QVector<ReportDefinition> standardReports =
        standardReportDefinitionsForDeck(workspace->deck(), pageReportName, rowReportName);
    for (const ReportDefinition& report : standardReports) {
        if (!hasReportNamed(report.name) && workspace->saveReportDefinition(-1, report)) {
            ++added;
        }
    }

    return added;
}

ReportPreviewData previewDataForCard(const Deck& deck, int cardIndex, const ReportDefinition& report)
{
    ReportPreviewData data;
    const int safeCardIndex = std::clamp(cardIndex, 0, std::max(0, deck.cardCount() - 1));
    const CardRecord& record = deck.cardAt(safeCardIndex);

    for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
        const QString name = deck.fieldAt(fieldIndex).name();
        const QString value = record.valueAt(fieldIndex);
        data.fieldValues.insert(name, value);
        data.fieldValues.insert(name.toLower(), value);
        data.fieldValues.insert(name.toUpper(), value);
    }

    const QDate today = QDate::currentDate();
    const QTime now = QTime::currentTime();
    data.systemValues.insert(QStringLiteral("reportname"), report.name);
    data.systemValues.insert(QStringLiteral("deckname"), deck.name());
    data.systemValues.insert(QStringLiteral("description"), deck.description());
    data.systemValues.insert(QStringLiteral("path"), QString());
    data.systemValues.insert(QStringLiteral("cardtotal"), QString::number(deck.cardCount()));
    data.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));
    data.systemValues.insert(QStringLiteral("date"), today.toString(QStringLiteral("M/d/yyyy")));
    data.systemValues.insert(QStringLiteral("shortdate"), today.toString(QStringLiteral("M/d/yyyy")));
    data.systemValues.insert(QStringLiteral("longdate"), today.toString(QStringLiteral("dddd, MMMM d, yyyy")));
    data.systemValues.insert(QStringLiteral("shortday"), today.toString(QStringLiteral("d")));
    data.systemValues.insert(QStringLiteral("longday"), today.toString(QStringLiteral("dd")));
    data.systemValues.insert(QStringLiteral("short weekday"), today.toString(QStringLiteral("ddd")));
    data.systemValues.insert(QStringLiteral("long weekday"), today.toString(QStringLiteral("dddd")));
    data.systemValues.insert(QStringLiteral("shortmonth"), today.toString(QStringLiteral("M")));
    data.systemValues.insert(QStringLiteral("longmonth"), today.toString(QStringLiteral("MMMM")));
    data.systemValues.insert(QStringLiteral("shortyear"), today.toString(QStringLiteral("yy")));
    data.systemValues.insert(QStringLiteral("longyear"), today.toString(QStringLiteral("yyyy")));
    data.systemValues.insert(QStringLiteral("time"), now.toString(QStringLiteral("h:mm AP")));
    data.systemValues.insert(QStringLiteral("longtime"), now.toString(QStringLiteral("h:mm:ss AP")));
    data.systemValues.insert(QStringLiteral("hour"), now.toString(QStringLiteral("h")));
    data.systemValues.insert(QStringLiteral("minutes"), now.toString(QStringLiteral("mm")));
    data.systemValues.insert(QStringLiteral("am/pm"), now.toString(QStringLiteral("AP")));
    return data;
}

PrintCardScope printScopeFromDialog(const QDialog& dialog)
{
    if (isChecked(dialog, Control::PrintAllCards)) {
        return PrintCardScope::AllCards;
    }
    if (isChecked(dialog, Control::PrintSelectedCards)) {
        return PrintCardScope::SelectedCards;
    }
    return PrintCardScope::ThisCard;
}

QString tokenForSystemBoxDialog(const QDialog& dialog)
{
    const QString selectedText = isChecked(dialog, Control::SystemBoxNumberCategory)
        ? comboText(dialog, Control::SystemBoxNumberFormats)
        : (isChecked(dialog, Control::SystemBoxSystemCategory)
                ? comboText(dialog, Control::SystemBoxSystemFields)
                : comboText(dialog, Control::SystemBoxDateFormats));
    const QString lower = selectedText.toLower();
    if (lower.contains(QStringLiteral("short date")) || lower.contains(QStringLiteral("date - short"))) {
        return QStringLiteral("{shortdate}");
    }
    if (lower.contains(QStringLiteral("long date")) || lower.contains(QStringLiteral("date - long"))) {
        return QStringLiteral("{longdate}");
    }
    if (lower.contains(QStringLiteral("day - short"))) {
        return QStringLiteral("{shortday}");
    }
    if (lower.contains(QStringLiteral("day - long"))) {
        return QStringLiteral("{longday}");
    }
    if (lower.contains(QStringLiteral("weekday - short"))) {
        return QStringLiteral("{short weekday}");
    }
    if (lower.contains(QStringLiteral("weekday - long"))) {
        return QStringLiteral("{long weekday}");
    }
    if (lower.contains(QStringLiteral("month - short"))) {
        return QStringLiteral("{shortmonth}");
    }
    if (lower.contains(QStringLiteral("month - long"))) {
        return QStringLiteral("{longmonth}");
    }
    if (lower.contains(QStringLiteral("year - short"))) {
        return QStringLiteral("{shortyear}");
    }
    if (lower.contains(QStringLiteral("year - long"))) {
        return QStringLiteral("{longyear}");
    }
    if (lower == QStringLiteral("hour")) {
        return QStringLiteral("{hour}");
    }
    if (lower.contains(QStringLiteral("minutes"))) {
        return QStringLiteral("{minutes}");
    }
    if (lower.contains(QStringLiteral("am/pm"))) {
        return QStringLiteral("{am/pm}");
    }
    if (lower.contains(QStringLiteral("cards"))) {
        return QStringLiteral("{cardtotal}");
    }
    if (lower.contains(QStringLiteral("deck file")) || lower.contains(QStringLiteral("path"))) {
        return QStringLiteral("{path}");
    }
    if (lower.contains(QStringLiteral("deck description"))) {
        return QStringLiteral("{description}");
    }
    if (lower.contains(QStringLiteral("deck name"))) {
        return QStringLiteral("{deckname}");
    }
    if (lower.contains(QStringLiteral("report"))) {
        return QStringLiteral("{reportname}");
    }
    if (lower.contains(QStringLiteral("page"))) {
        return QStringLiteral("{page}");
    }
    if (lower.contains(QStringLiteral("time"))) {
        return QStringLiteral("{time}");
    }
    if (lower.contains(QStringLiteral("long"))) {
        return QStringLiteral("{longdate}");
    }
    if (lower.contains(QStringLiteral("date"))) {
        return QStringLiteral("{date}");
    }
    if (isChecked(dialog, Control::SystemBoxNumberCategory)) {
        return QStringLiteral("{page}");
    }
    if (isChecked(dialog, Control::SystemBoxSystemCategory)) {
        return QStringLiteral("{reportname}");
    }
    return QStringLiteral("{date}");
}

QVector<ReportPreviewData> previewDataForDeck(
    const Deck& deck,
    const ReportDefinition& report,
    PrintCardScope scope,
    int currentCardIndex)
{
    QVector<ReportPreviewData> records;
    if (deck.cardCount() <= 0) {
        return records;
    }

    if (scope == PrintCardScope::AllCards) {
        records.reserve(deck.cardCount());
        for (int cardIndex = 0; cardIndex < deck.cardCount(); ++cardIndex) {
            records.append(previewDataForCard(deck, cardIndex, report));
        }
        return records;
    }

    records.append(previewDataForCard(deck, currentCardIndex, report));
    return records;
}

bool renderReportToPrinter(
    QPrinter* printer,
    const ReportDefinition& report,
    const QVector<ReportPreviewData>& records,
    const QVector<ReportPrintPage>& pages)
{
    if (printer == nullptr || records.isEmpty() || pages.isEmpty()) {
        return false;
    }

    QPainter painter;
    if (!painter.begin(printer)) {
        return false;
    }

    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        if (pageIndex > 0) {
            printer->newPage();
        }
        ReportPrintEngine::renderPage(
            &painter,
            report,
            records,
            pages.at(pageIndex),
            QRectF(painter.viewport()));
    }

    painter.end();
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget* parent, bool openInitialSample)
    : QMainWindow(parent)
    , m_mdiArea(new QMdiArea(this))
{
    initializeCardStackApplicationResources();
    setWindowTitle(QStringLiteral("CardStack"));
    m_quickDials = {
        {tr("Operator"), QStringLiteral("0")},
        {tr("Information"), QStringLiteral("411")},
    };
    if (windowIcon().isNull()) {
        setWindowIcon(cardStackIcon());
    }
    setCentralWidget(m_mdiArea);
    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, [this]() {
        refreshMenuForActiveWindow();
        updateCommandState();
    });

    createMenus();
    createToolBar();
    createIndexBar();
    qApp->installEventFilter(this);
    if (openInitialSample) {
        openSampleDeck();
    }
    updateCommandState();
}

MainWindow::~MainWindow()
{
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
    for (QObject* child : findChildren<QObject*>()) {
        disconnect(child, nullptr, this, nullptr);
    }
    if (m_mdiArea != nullptr) {
        for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList()) {
            disconnect(subWindow, nullptr, this, nullptr);
        }
        disconnect(m_mdiArea, nullptr, this, nullptr);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!closeAllSubWindowsWithPrompts()) {
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Close) {
        if (auto* subWindow = qobject_cast<QMdiSubWindow*>(watched)) {
            if (!confirmCloseDeckWindow(subWindow)) {
                static_cast<QCloseEvent*>(event)->ignore();
                return true;
            }
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (DeckWorkspace* workspace = activeDeckWorkspace()) {
            if (keyEvent->key() == Qt::Key_PageUp && keyEvent->modifiers() == Qt::NoModifier) {
                workspace->previousCard();
                updateCommandState();
                return true;
            }
            if (keyEvent->key() == Qt::Key_PageDown && keyEvent->modifiers() == Qt::NoModifier) {
                workspace->nextCard();
                updateCommandState();
                return true;
            }
            if (keyEvent->key() == Qt::Key_PageUp && keyEvent->modifiers() == Qt::ControlModifier) {
                workspace->previousCard();
                updateCommandState();
                return true;
            }
            if (keyEvent->key() == Qt::Key_PageDown && keyEvent->modifiers() == Qt::ControlModifier) {
                workspace->nextCard();
                updateCommandState();
                return true;
            }
        }
    }

    if (m_enterWorksLikeTab && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            QWidget* focusWidget = QApplication::focusWidget();
            const bool shouldAdvance = qobject_cast<QLineEdit*>(focusWidget) != nullptr
                || qobject_cast<QComboBox*>(focusWidget) != nullptr
                || qobject_cast<QSpinBox*>(focusWidget) != nullptr;
            if (shouldAdvance) {
                QWidget* nextWidget = focusWidget;
                do {
                    nextWidget = nextWidget->nextInFocusChain();
                } while (nextWidget != nullptr
                    && nextWidget != focusWidget
                    && (!nextWidget->isVisible()
                        || !nextWidget->isEnabled()
                        || nextWidget->focusPolicy() == Qt::NoFocus));
                if (nextWidget != nullptr && nextWidget != focusWidget) {
                    nextWidget->setFocus(Qt::TabFocusReason);
                }
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    resizeIndexBarButtons();
}

void MainWindow::createMenus()
{
    rebuildMenus(Menu::Startup);
}

void MainWindow::rebuildMenus(int menuId)
{
    if (m_currentMenuId == menuId && !menuBar()->actions().isEmpty()) {
        return;
    }

    menuBar()->clear();
    const bool populated = UiBuilder::populateMenuBar(menuBar(), menuId, this, [this](QAction* action) {
        if (action->data().toInt() == Command::RemovedCommand2800) {
            action->setVisible(false);
            action->setEnabled(false);
            return;
        }
        action->setEnabled(true);
        if (action->data().toInt() == Command::ConfigureShowButtonBar) {
            action->setCheckable(true);
            action->setChecked(m_buttonBar == nullptr || !m_buttonBar->isHidden());
        }
        connect(action, &QAction::triggered, this, &MainWindow::handleUiAction);
    });

    if (!populated) {
        QMessageBox::critical(this, tr("CardStack"), tr("Legacy menu resources could not be loaded."));
        return;
    }
    m_currentMenuId = menuId;
    updateWindowMenuEntries();
}

void MainWindow::refreshMenuForActiveWindow()
{
    if (activeReportDesigner() != nullptr) {
        rebuildMenus(Menu::ReportDesigner);
        configureToolBarForMenu(Menu::ReportDesigner);
        updateIndexBarVisibility();
        updateWindowMenuEntries();
        return;
    }
    if (activeTemplateDesigner() != nullptr) {
        rebuildMenus(Menu::TemplateDesigner);
        configureToolBarForMenu(Menu::TemplateDesigner);
        updateIndexBarVisibility();
        updateWindowMenuEntries();
        return;
    }
    const int menuId = activeDeckWorkspace() == nullptr ? Menu::Startup : Menu::MainDeck;
    rebuildMenus(menuId);
    configureToolBarForMenu(menuId);
    updateIndexBarVisibility();
    updateWindowMenuEntries();
}

void MainWindow::updateIndexBarVisibility()
{
    if (m_indexBar == nullptr) {
        return;
    }

    m_indexBar->setVisible(activeDeckWorkspace() != nullptr);
    resizeIndexBarButtons();
}

void MainWindow::updateToolbarCardPosition()
{
    if (m_cardPositionLabel == nullptr) {
        return;
    }

    const DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr || workspace->deck().cardCount() <= 0) {
        m_cardPositionLabel->clear();
        m_cardPositionLabel->setVisible(false);
        return;
    }

    m_cardPositionLabel->setVisible(true);
    m_cardPositionLabel->setText(tr("Card %1 of %2")
            .arg(workspace->currentCardIndex() + 1)
            .arg(workspace->deck().cardCount()));
}

void MainWindow::updateWindowMenuEntries()
{
    QMenu* windowMenu = nullptr;
    for (QAction* action : menuBar()->actions()) {
        QMenu* menu = action->menu();
        if (menu == nullptr) {
            continue;
        }
        QString title = menu->title();
        title.remove(QLatin1Char('&'));
        if (title == tr("Window")) {
            windowMenu = menu;
            break;
        }
    }
    if (windowMenu == nullptr) {
        return;
    }

    const QList<QAction*> actions = windowMenu->actions();
    for (QAction* action : actions) {
        if (action->property("cardstackDynamicWindowAction").toBool()) {
            windowMenu->removeAction(action);
            action->deleteLater();
        }
    }

    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList(QMdiArea::CreationOrder);
    if (windows.isEmpty()) {
        return;
    }

    QAction* separator = windowMenu->addSeparator();
    separator->setProperty("cardstackDynamicWindowAction", true);
    QMdiSubWindow* activeWindow = m_mdiArea->activeSubWindow();
    for (int index = 0; index < windows.size(); ++index) {
        QMdiSubWindow* subWindow = windows.at(index);
        QString title = subWindow->windowTitle();
        title.remove(QLatin1Char('&'));
        if (title.trimmed().isEmpty()) {
            title = tr("Window %1").arg(index + 1);
        }
        QAction* action = windowMenu->addAction(tr("&%1 %2").arg(index + 1).arg(title));
        action->setCheckable(true);
        action->setChecked(subWindow == activeWindow);
        action->setProperty("cardstackDynamicWindowAction", true);
        connect(action, &QAction::triggered, this, [this, subWindow]() {
            if (subWindow != nullptr) {
                subWindow->show();
                m_mdiArea->setActiveSubWindow(subWindow);
                updateWindowMenuEntries();
            }
        });
    }
}

void MainWindow::configureSubWindowSystemMenu(QMdiSubWindow* subWindow)
{
    if (subWindow == nullptr || subWindow->systemMenu() == nullptr) {
        return;
    }

    QMenu* menu = subWindow->systemMenu();
    for (QAction* action : menu->actions()) {
        if (action->property("cardstackWindowCycleAction").toBool()) {
            return;
        }
    }

    menu->addSeparator()->setProperty("cardstackWindowCycleAction", true);
    QAction* next = menu->addAction(tr("&Next\tCtrl+F6"));
    next->setProperty("cardstackWindowCycleAction", true);
    next->setShortcut(QKeySequence(QStringLiteral("Ctrl+F6")));
    connect(next, &QAction::triggered, m_mdiArea, &QMdiArea::activateNextSubWindow);

    QAction* previous = menu->addAction(tr("&Previous\tAlt+F6"));
    previous->setProperty("cardstackWindowCycleAction", true);
    previous->setShortcut(QKeySequence(QStringLiteral("Alt+F6")));
    connect(previous, &QAction::triggered, m_mdiArea, &QMdiArea::activatePreviousSubWindow);
}

void MainWindow::tileSubWindowsVertical()
{
    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
    if (windows.isEmpty()) {
        return;
    }

    const QRect area = m_mdiArea->viewport()->rect();
    const int windowCount = windows.size();
    const int width = std::max(1, area.width() / windowCount);
    for (int index = 0; index < windowCount; ++index) {
        const int left = area.left() + index * width;
        const int right = index + 1 == windowCount ? area.right() + 1 : left + width;
        windows.at(index)->setGeometry(left, area.top(), std::max(1, right - left), area.height());
    }
}

void MainWindow::tileSubWindowsHorizontal()
{
    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList(QMdiArea::ActivationHistoryOrder);
    if (windows.isEmpty()) {
        return;
    }

    const QRect area = m_mdiArea->viewport()->rect();
    const int windowCount = windows.size();
    const int height = std::max(1, area.height() / windowCount);
    for (int index = 0; index < windowCount; ++index) {
        const int top = area.top() + index * height;
        const int bottom = index + 1 == windowCount ? area.bottom() + 1 : top + height;
        windows.at(index)->setGeometry(area.left(), top, area.width(), std::max(1, bottom - top));
    }
}

void MainWindow::createToolBar()
{
    m_buttonBar = addToolBar(tr("Button Bar"));
    m_buttonBar->setObjectName(QStringLiteral("buttonBar"));
    m_buttonBar->setMovable(false);
    m_buttonBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_buttonBar->setIconSize(QSize(MainToolbarIconSizePx, MainToolbarIconSizePx));
    configureToolBarForMenu(Menu::Startup);
    updateCommandState();
}

void MainWindow::createIndexBar()
{
    addToolBarBreak(Qt::TopToolBarArea);
    m_indexBar = addToolBar(tr("Index Bar"));
    m_indexBar->setObjectName(QStringLiteral("indexBar"));
    m_indexBar->setMovable(false);
    m_indexBar->setFloatable(false);

    auto* container = new QWidget(m_indexBar);
    container->setObjectName(QStringLiteral("indexBarContainer"));
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(
        IndexBarHorizontalMarginPx,
        IndexBarVerticalMarginPx,
        IndexBarHorizontalMarginPx,
        IndexBarVerticalMarginPx);
    layout->setSpacing(IndexBarButtonGapPx);

    struct IndexButtonDefinition {
        QString label;
        QString key;
        QString statusText;
    };
    const QVector<IndexButtonDefinition> indexButtons = [] {
        QVector<IndexButtonDefinition> values{{QString(), QStringLiteral("SPACE"), QObject::tr("blank card titles")}};
        for (int code = 'A'; code <= 'Z'; ++code) {
            const QString letter = QString(QChar(code));
            values.append({letter, letter, letter});
        }
        for (int code = '0'; code <= '9'; ++code) {
            const QString digit = QString(QChar(code));
            values.append({digit, digit, digit});
        }
        return values;
    }();

    for (const IndexButtonDefinition& definition : indexButtons) {
        auto* button = new QPushButton(definition.label, container);
        button->setObjectName(QStringLiteral("index_%1").arg(definition.key));
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(false);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMinimumHeight(IndexBarButtonMinHeightPx);
        button->setMaximumHeight(IndexBarButtonMaxHeightPx);
        connect(button, &QPushButton::clicked, this, [this, key = definition.key, statusText = definition.statusText]() {
            DeckWorkspace* workspace = activeDeckWorkspace();
            if (workspace == nullptr) {
                return;
            }
            if (!workspace->jumpToIndexPrefix(key)) {
                statusBar()->showMessage(tr("No cards found for %1.").arg(statusText), StatusMessageTimeoutMs);
            }
            updateCommandState();
        });
        layout->addWidget(button);
    }

    m_indexBar->addWidget(container);
    resizeIndexBarButtons();
    m_indexBar->setVisible(false);
}

void MainWindow::resizeIndexBarButtons()
{
    if (m_indexBar == nullptr) {
        return;
    }

    auto* container = m_indexBar->findChild<QWidget*>(QStringLiteral("indexBarContainer"));
    if (container == nullptr) {
        return;
    }

    const QList<QPushButton*> buttons = container->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    if (buttons.isEmpty()) {
        return;
    }

    const int buttonCount = static_cast<int>(buttons.size());
    const int availableWidth = std::max(
        m_indexBar->contentsRect().width(),
        std::max(m_indexBar->width(), width()));
    const int marginWidth = IndexBarHorizontalMarginPx * 2;
    const int gapWidth = IndexBarButtonGapPx * std::max(0, buttonCount - 1);
    const int buttonWidth = std::max(1, (availableWidth - marginWidth - gapWidth) / buttonCount);
    const int usedWidth = buttonWidth * buttonCount + gapWidth + marginWidth;

    container->setFixedWidth(usedWidth);
    for (QPushButton* button : buttons) {
        button->setMinimumWidth(buttonWidth);
        button->setMaximumWidth(buttonWidth);
    }
}

void MainWindow::configureToolBarForMenu(int menuId)
{
    if (m_buttonBar == nullptr) {
        return;
    }

    if (m_buttonBar->property("cardstackToolbarMenuId").toInt() == menuId && !m_buttonBar->actions().isEmpty()) {
        return;
    }

    m_buttonBar->clear();
    m_cardPositionLabel = nullptr;
    m_buttonBar->setProperty("cardstackToolbarMenuId", menuId);

    const auto toolbarIcon = [](const QString& iconName) {
        QIcon icon;
        icon.addFile(QStringLiteral(":/cardstack/toolbar/%1.svg").arg(iconName));
        icon.addFile(QStringLiteral(":/cardstack/toolbar/png-24/%1.png").arg(iconName), QSize(24, 24));
        icon.addFile(QStringLiteral(":/cardstack/toolbar/png-48/%1.png").arg(iconName), QSize(48, 48));
        return icon;
    };

    const auto findPersistentAction = [this](int commandId) -> QAction* {
        if (QAction* action = findUiAction(commandId)) {
            return action;
        }
        for (QAction* action : findChildren<QAction*>()) {
            if (action->data().toInt() == commandId) {
                return action;
            }
        }
        return nullptr;
    };

    const auto addUiToolAction = [this, toolbarIcon, findPersistentAction](int commandId, const QString& iconName, const QString& toolTip) {
        QAction* action = findPersistentAction(commandId);
        if (action == nullptr) {
            action = new QAction(toolTip, this);
            action->setData(commandId);
            action->setEnabled(false);
            connect(action, &QAction::triggered, this, &MainWindow::handleUiAction);
        }
        action->setIcon(toolbarIcon(iconName));
        action->setToolTip(toolTip);
        action->setIconText(QString());
        m_buttonBar->addAction(action);
    };

    const auto addSeparator = [this]() {
        if (!m_buttonBar->actions().isEmpty()) {
            m_buttonBar->addSeparator();
        }
    };
    const auto addCardPositionLabel = [this]() {
        auto* spacer = new QWidget(m_buttonBar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_buttonBar->addWidget(spacer);

        m_cardPositionLabel = new QLabel(m_buttonBar);
        m_cardPositionLabel->setObjectName(QStringLiteral("toolbarCardPositionLabel"));
        m_cardPositionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cardPositionLabel->setMinimumWidth(120);
        m_cardPositionLabel->setContentsMargins(8, 0, 8, 0);
        m_buttonBar->addWidget(m_cardPositionLabel);
        updateToolbarCardPosition();
    };

    addUiToolAction(Command::FileNew, QStringLiteral("deck-new"), tr("New deck"));
    addUiToolAction(Command::FileOpen, QStringLiteral("deck-open"), tr("Open deck"));
    if (menuId == Menu::Startup) {
        return;
    }

    addUiToolAction(Command::FileSave, QStringLiteral("deck-save"), tr("Save deck"));
    if (menuId == Menu::TemplateDesigner) {
        addSeparator();
        addUiToolAction(Command::ToolAddText, QStringLiteral("tool-text"), tr("Add template text"));
        addUiToolAction(Command::ToolAddDataBox, QStringLiteral("tool-data-box"), tr("Add template data box"));
        addUiToolAction(Command::ToolAddNotesBox, QStringLiteral("tool-notes-box"), tr("Add template notes box"));
        addUiToolAction(Command::ToolAddLineOrBox, QStringLiteral("tool-line-box"), tr("Add template line or box"));
        return;
    }

    if (menuId == Menu::ReportDesigner) {
        addUiToolAction(Command::FileSaveAs, QStringLiteral("report-open"), tr("Export report package"));
        addSeparator();
        addUiToolAction(Command::ToolAddText, QStringLiteral("tool-text"), tr("Add report text"));
        addUiToolAction(Command::ToolAddDataBox, QStringLiteral("tool-data-box"), tr("Add report data box"));
        addUiToolAction(Command::ToolAddSystemData, QStringLiteral("tool-system-data"), tr("Add report system data"));
        addUiToolAction(Command::ToolAddLineOrBox, QStringLiteral("tool-line-box"), tr("Add report line or box"));
        addUiToolAction(Command::ToolChangeForm, QStringLiteral("tool-form"), tr("Change report form"));
        return;
    }

    addUiToolAction(Command::FilePrintReport, QStringLiteral("print-report"), tr("Print report"));
    addSeparator();
    addUiToolAction(Command::SearchFind, QStringLiteral("find"), tr("Find"));
    addUiToolAction(Command::SearchReplace, QStringLiteral("replace"), tr("Replace"));
    addSeparator();
    addUiToolAction(Command::CardAdd, QStringLiteral("card-add"), tr("Add card"));
    addUiToolAction(Command::CardDuplicate, QStringLiteral("card-duplicate"), tr("Duplicate card"));
    addUiToolAction(Command::CardDelete, QStringLiteral("card-delete"), tr("Delete card"));
    addUiToolAction(Command::CardUndelete, QStringLiteral("card-undelete"), tr("Undelete card"));
    addSeparator();
    addUiToolAction(Command::NavigateFirstCard, QStringLiteral("nav-first"), tr("First card"));
    addUiToolAction(Command::NavigatePreviousCard, QStringLiteral("nav-previous"), tr("Previous card"));
    addUiToolAction(Command::NavigateNextCard, QStringLiteral("nav-next"), tr("Next card"));
    addUiToolAction(Command::NavigateLastCard, QStringLiteral("nav-last"), tr("Last card"));
    addSeparator();
    addUiToolAction(Command::FileNewReport, QStringLiteral("report-new"), tr("New report"));
    addUiToolAction(Command::FileOpenReport, QStringLiteral("report-open"), tr("Open report designer"));
    addCardPositionLabel();
}

void MainWindow::handleUiAction()
{
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    const int commandId = action->data().toInt();
    if (activeReportDesigner() != nullptr && isReportDesignerCommand(commandId)) {
        handleReportDesignerCommand(commandId);
        updateCommandState();
        return;
    }

    if (activeTemplateDesigner() != nullptr && isTemplateDesignerCommand(commandId)) {
        handleTemplateDesignerCommand(commandId);
        updateCommandState();
        return;
    }

    if (isDeckCommand(commandId)) {
        handleDeckCommand(commandId);
        updateCommandState();
        return;
    }

    switch (commandId) {
    case Command::FileNew:
        handleNewDeckCommand();
        return;
    case Command::FileOpen:
        handleOpenCommand();
        return;
    case Command::FileMerge:
        handleMergeCommand();
        return;
    case Command::FilePrintReport:
        handlePrintReportCommand();
        return;
    case Command::FilePrinterSetup:
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageMargins(
            QMarginsF(
                DefaultReportMarginMils / 1000.0,
                DefaultReportMarginMils / 1000.0,
                DefaultReportMarginMils / 1000.0,
                DefaultReportMarginMils / 1000.0),
            QPageLayout::Inch);
        QPageSetupDialog pageSetup(&printer, this);
        pageSetup.exec();
        return;
    }
    case Command::FileSave:
        saveActiveDeck();
        return;
    case Command::FileSaveAs:
        saveActiveDeckAs();
        return;
    case Command::FileClose:
        if (QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow()) {
            subWindow->close();
        }
        updateCommandState();
        return;
    case Command::FileNewReport:
        handleNewReportCommand();
        return;
    case Command::FileExit:
        close();
        return;
    case Command::WindowTileVertical:
        tileSubWindowsVertical();
        return;
    case Command::WindowTileHorizontal:
        tileSubWindowsHorizontal();
        return;
    case Command::WindowCascade:
        m_mdiArea->cascadeSubWindows();
        return;
    case Command::WindowArrangeIcons:
        statusBar()->showMessage(tr("Arrange Icons is retained for menu parity; minimized MDI icon layout has no Qt equivalent yet."), StatusMessageTimeoutMs);
        return;
    case Command::WindowCloseAll:
        closeAllSubWindowsWithPrompts();
        updateCommandState();
        return;
    case Command::ConfigurePhoneDialer:
        handlePhoneDialerConfigCommand();
        return;
    case Command::PhoneDial:
        handlePhoneDialCommand();
        return;
    case Command::ConfigureAddSecurity:
        handleSecurityCommand();
        return;
    case Command::ConfigureDeckDescription:
        handleDeckDescriptionCommand();
        return;
    case Command::ConfigureShowButtonBar:
        if (m_buttonBar != nullptr) {
            m_buttonBar->setVisible(!m_buttonBar->isVisible());
        }
        updateCommandState();
        return;
    case Command::ConfigureEnterWorksLikeTab:
        m_enterWorksLikeTab = !m_enterWorksLikeTab;
        action->setChecked(m_enterWorksLikeTab);
        statusBar()->showMessage(
            m_enterWorksLikeTab ? tr("ENTER now advances focus like TAB.") : tr("ENTER uses the default control behavior."), StatusMessageTimeoutMs);
        return;
    case Command::HelpContents:
        showHelpContents();
        return;
    case Command::HelpAbout:
        showAboutDialog();
        return;
    default:
        break;
    }

    const QString dialogName = dialogNameForCommand(commandId);
    if (!dialogName.isEmpty()) {
        showUiDialog(dialogName);
        return;
    }

    showUiCommandStatus(commandId);
}

void MainWindow::handleOpenCommand()
{
    const QString path = chooseOpenPath();
    if (!path.isEmpty()) {
        openDeckFromPath(path);
    }
}

void MainWindow::handleDeckCommand(int commandId)
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    switch (commandId) {
    case Command::FileRedefine:
        handleRedefineCommand();
        return;
    case Command::FileMerge:
        handleMergeCommand();
        return;
    case Command::FilePrintReport:
        handlePrintReportCommand();
        return;
    case Command::FileNewReport:
        handleNewReportCommand();
        return;
    case Command::EditUndo:
        if (workspace->undo()) {
            statusBar()->showMessage(tr("Last deck change undone."), StatusMessageTimeoutMs);
        } else {
            statusBar()->showMessage(tr("Nothing to undo."), StatusMessageTimeoutMs);
        }
        return;
    case Command::EditCut:
        workspace->cut();
        return;
    case Command::EditCopy:
        workspace->copy();
        return;
    case Command::EditPaste:
        workspace->paste();
        return;
    case Command::EditSmartPaste:
        workspace->smartPaste();
        return;
    case Command::EditClear:
        workspace->clearCurrentValue();
        return;
    case Command::CardAdd:
        workspace->addCard();
        return;
    case Command::CardDelete:
        if (QMessageBox::question(this, tr("CardStack"), tr("Delete the current card?")) == QMessageBox::Yes) {
            workspace->deleteCurrentCard();
        }
        return;
    case Command::CardDuplicate:
        workspace->duplicateCurrentCard();
        return;
    case Command::CardUndelete:
        if (workspace->undeleteCard()) {
            statusBar()->showMessage(tr("Deleted card restored."), StatusMessageTimeoutMs);
        } else {
            statusBar()->showMessage(tr("No deleted card is available to undelete."), StatusMessageTimeoutMs);
        }
        return;
    case Command::ViewCard:
        workspace->showCardView();
        return;
    case Command::ViewTable:
        workspace->showTableView();
        return;
    case Command::SearchFindNext:
        handleFindNextCommand();
        return;
    case Command::SearchFind:
        handleFindCommand();
        return;
    case Command::SearchReplace:
        handleReplaceCommand();
        return;
    case Command::ConfigureDataFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(&accepted, workspace->font(), this, tr("Data Font"));
        if (accepted) {
            workspace->applyDataFont(font);
        }
        return;
    }
    case Command::ConfigureNameFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(&accepted, workspace->font(), this, tr("Name Font"));
        if (accepted) {
            workspace->applyNameFont(font);
        }
        return;
    }
    case Command::ConfigureTextFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(&accepted, workspace->font(), this, tr("Text Font"));
        if (accepted) {
            workspace->applyDataFont(font);
        }
        return;
    }
    case Command::ConfigureIndexFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(&accepted, workspace->font(), this, tr("Index Font"));
        if (accepted) {
            workspace->applyIndexFont(font);
        }
        return;
    }
    case Command::ConfigureColors:
        showUiDialog(QStringLiteral("CHOOSECOLOR"));
        return;
    case Command::ConfigureIndex:
        handleSortCommand();
        return;
    case Command::NavigateFirstCard:
        workspace->firstCard();
        return;
    case Command::NavigateLastCard:
        workspace->lastCard();
        return;
    case Command::NavigatePreviousWindowful:
    case Command::NavigatePreviousCard:
        workspace->previousCard();
        return;
    case Command::NavigateNextCard:
    case Command::NavigateNextWindowful:
        workspace->nextCard();
        return;
    default:
        break;
    }

    showUiCommandStatus(commandId);
}

void MainWindow::handleFindCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SEARCH"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Find dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    if (workspace->find(searchRequestFromDialog(*dialog))) {
        statusBar()->showMessage(tr("Match found."), StatusMessageTimeoutMs);
    } else {
        QMessageBox::information(this, tr("CardStack"), tr("No matching card was found."));
    }
}

void MainWindow::handleFindNextCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    if (workspace->findNext()) {
        statusBar()->showMessage(tr("Next match found."), StatusMessageTimeoutMs);
    } else {
        QMessageBox::information(this, tr("CardStack"), tr("No further matching card was found."));
    }
}

void MainWindow::handleReplaceCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("REPLACE"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Replace dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    dialog->setProperty("replaceCommand", QStringLiteral("replaceCurrent"));
    if (auto* replaceAllButton = uiControl<QAbstractButton>(*dialog, Control::SearchDirectionBeginning)) {
        connect(replaceAllButton, &QAbstractButton::clicked, dialog.get(), [dialog = dialog.get()]() {
            dialog->setProperty("replaceCommand", QStringLiteral("replaceAll"));
            dialog->accept();
        });
    }
    if (auto* findNextButton = uiControl<QAbstractButton>(*dialog, Control::SearchDirectionForward)) {
        connect(findNextButton, &QAbstractButton::clicked, dialog.get(), [this, workspace, dialog = dialog.get()]() {
            DeckWorkspace::SearchRequest request = searchRequestFromDialog(*dialog);
            request.direction = DeckWorkspace::SearchDirection::ForwardFromCurrent;
            if (!workspace->find(request)) {
                QMessageBox::information(this, tr("CardStack"), tr("No further matching card was found."));
            }
        });
    }
    if (auto* findPreviousButton = uiControl<QAbstractButton>(*dialog, Control::SearchDirectionBackward)) {
        connect(findPreviousButton, &QAbstractButton::clicked, dialog.get(), [this, workspace, dialog = dialog.get()]() {
            DeckWorkspace::SearchRequest request = searchRequestFromDialog(*dialog);
            request.direction = DeckWorkspace::SearchDirection::BackwardFromCurrent;
            if (!workspace->find(request)) {
                QMessageBox::information(this, tr("CardStack"), tr("No previous matching card was found."));
            }
        });
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    const DeckWorkspace::SearchRequest request = searchRequestFromDialog(*dialog);
    const QString replacement = comboText(*dialog, Control::SearchSecondText);
    if (dialog->property("replaceCommand").toString() == QStringLiteral("replaceAll")) {
        const int count = workspace->replaceAll(request, replacement);
        statusBar()->showMessage(tr("Replaced %1 value(s).").arg(count), StatusMessageTimeoutMs);
        return;
    }

    if (workspace->replaceCurrent(request, replacement)) {
        statusBar()->showMessage(tr("Current match replaced."), StatusMessageTimeoutMs);
    } else {
        QMessageBox::information(this, tr("CardStack"), tr("No matching value was found to replace."));
    }
}

void MainWindow::handleSortCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SORT"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Index dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    const QVector<DeckWorkspace::SortLevel> levels = sortLevelsFromDialog(*dialog);
    if (levels.isEmpty()) {
        statusBar()->showMessage(tr("No index fields were selected."), StatusMessageTimeoutMs);
        return;
    }

    workspace->sortCards(levels);
    statusBar()->showMessage(tr("Deck index changed."), StatusMessageTimeoutMs);
}

void MainWindow::handleRedefineCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    workspace->commitPendingEdits();
    openTemplateDesigner(workspace);
}

void MainWindow::handleMergeCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    const QString sourcePath = chooseMergeSourcePath();
    if (sourcePath.isEmpty()) {
        return;
    }

    Deck sourceDeck;
    QString error;
    if (!loadDeckFromPath(sourcePath, &sourceDeck, &error)) {
        QMessageBox::critical(this, tr("CardStack Merge"), tr("Could not open merge source:\n%1").arg(error));
        return;
    }

    if (sourceDeck.fieldCount() <= 0 || sourceDeck.cardCount() <= 0) {
        QMessageBox::information(this, tr("CardStack Merge"), tr("The merge source has no fields or cards to merge."));
        return;
    }

    UiBuilder::DialogContext context = dialogContext();
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("MERGEDLG"), this, context);
    if (!dialog) {
        statusBar()->showMessage(tr("Merge dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (auto* sourceFile = uiControl<QLabel>(*dialog, Control::MergeSourceFile)) {
        sourceFile->setText(QFileInfo(sourcePath).fileName());
    }
    if (auto* destinationFile = uiControl<QLabel>(*dialog, Control::MergeDestinationFile)) {
        destinationFile->setText(workspace->deck().name());
    }

    auto* sourceList = uiControl<QListWidget>(*dialog, Control::MappingSourceList);
    auto* destinationList = uiControl<QListWidget>(*dialog, Control::MappingDestinationList);
    if (sourceList == nullptr || destinationList == nullptr) {
        statusBar()->showMessage(tr("Merge field lists are not available."), StatusMessageTimeoutMs);
        return;
    }

    sourceList->clear();
    sourceList->addItems(fieldNamesFromDeck(sourceDeck));
    if (sourceList->count() > 0) {
        sourceList->setCurrentRow(0);
    }

    QVector<int> mappings(workspace->deck().fieldCount(), -1);
    auto refreshDestinationList = [destinationList, &mappings, &sourceDeck, workspace]() {
        const int previousRow = std::max(0, destinationList->currentRow());
        destinationList->clear();
        for (int destinationIndex = 0; destinationIndex < workspace->deck().fieldCount(); ++destinationIndex) {
            auto* item = new QListWidgetItem(
                mergeDestinationText(workspace->deck(), sourceDeck, destinationIndex, mappings.at(destinationIndex)));
            item->setData(Qt::UserRole, destinationIndex);
            item->setData(Qt::UserRole + 1, mappings.at(destinationIndex));
            destinationList->addItem(item);
        }
        if (destinationList->count() > 0) {
            destinationList->setCurrentRow(std::clamp(previousRow, 0, destinationList->count() - 1));
        }
    };

    auto mapCurrent = [sourceList, destinationList, &mappings, refreshDestinationList]() {
        const int sourceIndex = sourceList->currentRow();
        const int destinationRow = destinationList->currentRow();
        if (sourceIndex < 0 || destinationRow < 0 || destinationRow >= mappings.size()) {
            return;
        }
        mappings[destinationRow] = sourceIndex;
        refreshDestinationList();
    };

    auto mapAll = [&mappings, &sourceDeck, workspace, refreshDestinationList]() {
        for (int destinationIndex = 0; destinationIndex < mappings.size(); ++destinationIndex) {
            mappings[destinationIndex] = bestSourceFieldForDestination(sourceDeck, workspace->deck(), destinationIndex);
        }
        refreshDestinationList();
    };

    auto removeCurrent = [destinationList, &mappings, refreshDestinationList]() {
        const int destinationRow = destinationList->currentRow();
        if (destinationRow < 0 || destinationRow >= mappings.size()) {
            return;
        }
        mappings[destinationRow] = -1;
        refreshDestinationList();
    };

    auto removeAll = [&mappings, refreshDestinationList]() {
        std::fill(mappings.begin(), mappings.end(), -1);
        refreshDestinationList();
    };

    if (auto* button = uiControl<QAbstractButton>(*dialog, Control::MappingAdd)) {
        connect(button, &QAbstractButton::clicked, dialog.get(), mapCurrent);
    }
    if (auto* button = uiControl<QAbstractButton>(*dialog, Control::MappingAddAll)) {
        connect(button, &QAbstractButton::clicked, dialog.get(), mapAll);
    }
    if (auto* button = uiControl<QAbstractButton>(*dialog, Control::MappingRemove)) {
        connect(button, &QAbstractButton::clicked, dialog.get(), removeCurrent);
    }
    if (auto* button = uiControl<QAbstractButton>(*dialog, Control::MappingRemoveAll)) {
        connect(button, &QAbstractButton::clicked, dialog.get(), removeAll);
    }
    connect(sourceList, &QListWidget::itemDoubleClicked, dialog.get(), [mapCurrent](QListWidgetItem*) {
        mapCurrent();
    });
    connect(destinationList, &QListWidget::itemDoubleClicked, dialog.get(), [removeCurrent](QListWidgetItem*) {
        removeCurrent();
    });

    mapAll();

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    DeckMergeOptions options;
    options.scope = isChecked(*dialog, Control::SelectedCards)
        ? DeckMergeOptions::Scope::SelectedCards
        : DeckMergeOptions::Scope::AllCards;
    if (options.scope == DeckMergeOptions::Scope::SelectedCards) {
        for (int cardIndex = 0; cardIndex < sourceDeck.cardCount(); ++cardIndex) {
            options.selectedSourceCardIndexes.append(cardIndex);
        }
    }
    for (int destinationIndex = 0; destinationIndex < mappings.size(); ++destinationIndex) {
        if (mappings.at(destinationIndex) >= 0) {
            options.fieldMappings.append({mappings.at(destinationIndex), destinationIndex});
        }
    }

    workspace->commitPendingEdits();
    const DeckMergeResult result = workspace->mergeFromDeck(sourceDeck, options);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("CardStack Merge"), result.errorMessage);
        return;
    }

    statusBar()->showMessage(
        tr("Merged %1 card(s) from %2.").arg(result.cardsMerged).arg(QFileInfo(sourcePath).fileName()), StatusMessageTimeoutMs);
    updateCommandState();
}

void MainWindow::handleNewReportCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    ReportDefinition report = createDefaultReportDefinition(workspace->deck(), tr("Untitled Report"));
    if (!configureReportForm(&report)) {
        return;
    }

    openReportDesigner(workspace, -1, report);
}

void MainWindow::handlePrintReportCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    struct DeletedReport {
        ReportDefinition report;
        int index = -1;
    };
    std::optional<DeletedReport> deletedReport;

    int reportIndex = -1;
    for (;;) {
        std::unique_ptr<QDialog> reportsDialog =
            UiBuilder::createDialog(QStringLiteral("DESIGNREPORTS"), this, dialogContext());
        if (!reportsDialog) {
            statusBar()->showMessage(tr("Available Reports dialog is not available."), StatusMessageTimeoutMs);
            return;
        }

        QString reportAction = QStringLiteral("print");
        if (auto* modifyButton = uiControl<QAbstractButton>(*reportsDialog, Control::ReportsModify)) {
            connect(modifyButton, &QAbstractButton::clicked, reportsDialog.get(), [&reportAction, dialog = reportsDialog.get()]() {
                reportAction = QStringLiteral("modify");
                dialog->accept();
            });
        }
        if (auto* newButton = uiControl<QAbstractButton>(*reportsDialog, Control::ReportsNew)) {
            connect(newButton, &QAbstractButton::clicked, reportsDialog.get(), [&reportAction, dialog = reportsDialog.get()]() {
                reportAction = QStringLiteral("new");
                dialog->accept();
            });
        }
        if (auto* deleteButton = uiControl<QAbstractButton>(*reportsDialog, Control::ReportsDelete)) {
            connect(deleteButton, &QAbstractButton::clicked, reportsDialog.get(), [&reportAction, dialog = reportsDialog.get()]() {
                reportAction = QStringLiteral("delete");
                dialog->accept();
            });
        }
        if (auto* addDefaultsButton = uiControl<QAbstractButton>(*reportsDialog, Control::ReportsAddDefaults)) {
            connect(addDefaultsButton, &QAbstractButton::clicked, reportsDialog.get(), [&reportAction, dialog = reportsDialog.get()]() {
                reportAction = QStringLiteral("defaults");
                dialog->accept();
            });
        }
        if (auto* undoButton = uiControl<QAbstractButton>(*reportsDialog, Control::ReportsUndoDelete)) {
            undoButton->setEnabled(deletedReport.has_value());
            connect(undoButton, &QAbstractButton::clicked, reportsDialog.get(), [&reportAction, dialog = reportsDialog.get()]() {
                reportAction = QStringLiteral("undo-delete");
                dialog->accept();
            });
        }

        if (reportsDialog->exec() != QDialog::Accepted) {
            return;
        }

        const auto* reportList = uiControl<QListWidget>(*reportsDialog, Control::ReportsList);
        reportIndex = reportList == nullptr ? -1 : reportList->currentRow();
        if (reportAction == QStringLiteral("new")) {
            openReportDesigner(
                workspace,
                -1,
                createDefaultReportDefinition(workspace->deck(), tr("Untitled Report")));
            return;
        }
        if (reportAction == QStringLiteral("defaults")) {
            const int added = addStandardReportDefinitions(workspace);
            statusBar()->showMessage(tr("Added %1 default report(s).").arg(added), StatusMessageTimeoutMs);
            updateCommandState();
            continue;
        }
        if (reportAction == QStringLiteral("undo-delete")) {
            if (deletedReport.has_value() && workspace->insertReportDefinition(deletedReport->index, deletedReport->report)) {
                deletedReport.reset();
                statusBar()->showMessage(tr("Report design restored."), StatusMessageTimeoutMs);
            }
            updateCommandState();
            continue;
        }
        if (reportIndex < 0 || reportIndex >= workspace->deck().reportCount()) {
            statusBar()->showMessage(tr("No report was selected."), StatusMessageTimeoutMs);
            continue;
        }

        if (reportAction == QStringLiteral("modify")) {
            openReportDesigner(workspace, reportIndex, workspace->deck().reportAt(reportIndex));
            return;
        }
        if (reportAction == QStringLiteral("delete")) {
            deletedReport = DeletedReport{workspace->deck().reportAt(reportIndex), reportIndex};
            if (workspace->removeReportDefinition(reportIndex)) {
                statusBar()->showMessage(tr("Report design deleted. Use Undo Del to restore it."), StatusMessageTimeoutMs);
                updateCommandState();
            }
            continue;
        }

        break;
    }

    const ReportDefinition& report = workspace->deck().reportAt(reportIndex);
    UiBuilder::DialogContext printContext = dialogContext();
    printContext.reportNames = {report.name};

    std::unique_ptr<QDialog> optionsDialog =
        UiBuilder::createDialog(QStringLiteral("PRINT"), this, printContext);
    if (!optionsDialog) {
        statusBar()->showMessage(tr("Print dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    const Deck deckSnapshot = workspace->deck();
    const int currentCardIndex = workspace->currentCardIndex();
    auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    if (report.formWidth > report.formHeight) {
        printer->setPageOrientation(QPageLayout::Landscape);
    }

    const auto buildRecords = [deckSnapshot, report, currentCardIndex](const QDialog& dialog) {
        return previewDataForDeck(deckSnapshot, report, printScopeFromDialog(dialog), currentCardIndex);
    };

    const auto previewSelectedScope = [this, printContext, report, buildRecords](const QDialog& dialog) {
        const QVector<ReportPreviewData> records = buildRecords(dialog);
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        if (records.isEmpty() || pages.isEmpty()) {
            QMessageBox::information(this, tr("CardStack Reports"), tr("No cards are available for this report."));
            return;
        }
        ReportPreviewDialog::exec(this, printContext, report, records, pages);
    };

    if (auto* previewButton = uiControl<QAbstractButton>(*optionsDialog, Control::PrintPreview)) {
        connect(previewButton, &QAbstractButton::clicked, optionsDialog.get(), [previewSelectedScope, dialog = optionsDialog.get()]() {
            previewSelectedScope(*dialog);
        });
    }
    if (auto* setupButton = uiControl<QAbstractButton>(*optionsDialog, Control::PrintPrinterSetup)) {
        connect(setupButton, &QAbstractButton::clicked, optionsDialog.get(), [this, printer]() {
            printer->setPageMargins(
                QMarginsF(
                    DefaultReportMarginMils / 1000.0,
                    DefaultReportMarginMils / 1000.0,
                    DefaultReportMarginMils / 1000.0,
                    DefaultReportMarginMils / 1000.0),
                QPageLayout::Inch);
            QPageSetupDialog pageSetup(printer.get(), this);
            pageSetup.exec();
        });
    }
    if (auto* defineSearchButton = uiControl<QAbstractButton>(*optionsDialog, Control::PrintDefineSearch)) {
        connect(defineSearchButton, &QAbstractButton::clicked, optionsDialog.get(), [this]() {
            handleFindCommand();
        });
    }

    if (optionsDialog->exec() != QDialog::Accepted) {
        return;
    }

    const QVector<ReportPreviewData> records = buildRecords(*optionsDialog);
    const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
    if (records.isEmpty() || pages.isEmpty()) {
        QMessageBox::information(this, tr("CardStack Reports"), tr("No cards are available for this report."));
        return;
    }

    QPrintDialog nativePrintDialog(printer.get(), this);
    nativePrintDialog.setWindowTitle(tr("Print %1").arg(report.name));
    if (nativePrintDialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!renderReportToPrinter(printer.get(), report, records, pages)) {
        QMessageBox::critical(this, tr("CardStack Reports"), tr("Could not render the report to the selected printer."));
        return;
    }

    statusBar()->showMessage(tr("Printed %1 page(s) for %2.").arg(pages.size()).arg(report.name), StatusMessageTimeoutMs);
}

void MainWindow::handleReportDesignerCommand(int commandId)
{
    ReportDesignerWidget* designer = activeReportDesigner();
    if (designer == nullptr) {
        showUiCommandStatus(commandId);
        return;
    }

    auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
    switch (commandId) {
    case Command::FileNewReport:
        if (owner != nullptr) {
            openReportDesigner(
                owner,
                owner->deck().reportCount(),
                createDefaultReportDefinition(owner->deck(), tr("Untitled Report")));
        } else {
            statusBar()->showMessage(tr("The source deck window is no longer available."), StatusMessageTimeoutMs);
        }
        return;
    case Command::FileOpenReport: {
        if (owner == nullptr || owner->deck().reportCount() == 0) {
            statusBar()->showMessage(tr("No saved reports are available."), StatusMessageTimeoutMs);
            return;
        }

        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("DESIGNREPORTS"), this, dialogContext());
        if (!dialog) {
            statusBar()->showMessage(tr("Available Reports dialog is not available."), StatusMessageTimeoutMs);
            return;
        }
        if (dialog->exec() != QDialog::Accepted) {
            return;
        }
        auto* list = uiControl<QListWidget>(*dialog, Control::ReportsList);
        const int reportIndex = list == nullptr ? 0 : std::max(0, list->currentRow());
        if (reportIndex < owner->deck().reportCount()) {
            openReportDesigner(owner, reportIndex, owner->deck().reportAt(reportIndex));
        }
        return;
    }
    case Command::FileCloseDeck:
        if (owner != nullptr) {
            if (QWidget* ownerWindow = owner->parentWidget()) {
                ownerWindow->close();
            }
        }
        return;
    case Command::FileClose:
        if (QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow()) {
            subWindow->close();
        }
        return;
    case Command::FileSaveReport:
    case Command::FileSaveReportAs:
        if (commandId == Command::FileSaveReportAs) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SAVEDESIGN"), this, dialogContext());
            if (dialog && dialog->exec() != QDialog::Accepted) {
                return;
            }
            if (dialog) {
                if (auto* nameEdit = uiControl<QLineEdit>(*dialog, Control::SaveDesignName)) {
                    const QString reportName = nameEdit->text().trimmed();
                    auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
                    bool ownerReportIndexOk = false;
                    const int parsedOwnerReportIndex = designer->property("ownerReportIndex").toInt(&ownerReportIndexOk);
                    const int ownerReportIndex = ownerReportIndexOk ? parsedOwnerReportIndex : -1;
                    if (reportNameExists(owner, reportName, ownerReportIndex)) {
                        QMessageBox::warning(
                            this,
                            tr("Save Report As"),
                            tr("A report named \"%1\" already exists. Choose a unique report name.").arg(reportName));
                        return;
                    }
                    designer->setReportName(reportName);
                }
            }
        }
        designer->save();
        return;
    case Command::FileSaveAs:
        exportReportPackageFromDesigner(designer);
        return;
    case Command::EditUndo:
        statusBar()->showMessage(tr("Report-designer undo is deferred; use Save/Close prompts to preserve or discard changes."), StatusMessageTimeoutMs);
        return;
    case Command::EditCut:
    case Command::EditCopy:
    case Command::EditPaste:
        statusBar()->showMessage(tr("Report frame clipboard editing is deferred; use Add Frame and Delete Frame in the designer."), StatusMessageTimeoutMs);
        return;
    case Command::EditClear:
        designer->deleteSelectedFrame();
        return;
    case Command::ToolAddText:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TEXTFRAME"), this, dialogContext());
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        const QString text = dialog ? editText(*dialog, Control::FrameText) : QString();
        designer->addTextFrameWithText(text, dialog ? frameStyleFlagsFromDialog(*dialog) : 0);
        return;
    }
    case Command::ToolAddDataBox:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("DATAFRAME"), this, dialogContext());
        if (dialog) {
            populateComboIfEmpty(*dialog, Control::DataFrameFieldList, designer->fieldNames());
        }
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        QString fieldName;
        if (dialog) {
            fieldName = comboText(*dialog, Control::DataFrameFieldList);
            if (fieldName.isEmpty()) {
                if (auto* edit = uiControl<QLineEdit>(*dialog, Control::FrameText)) {
                    fieldName = edit->text();
                }
            }
        }
        designer->addDataFrameForField(fieldName, dialog ? frameStyleFlagsFromDialog(*dialog) : 0, dialog && isChecked(*dialog, Control::DataFramePrintEntireContents));
        return;
    }
    case Command::ToolAddSystemData:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("ADDSYSTEMBOX"), this, dialogContext());
        if (dialog) {
            initializeSystemBoxDialog(*dialog);
        }
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        designer->addSystemFrameWithText(dialog ? tokenForSystemBoxDialog(*dialog) : QStringLiteral("{page}"), dialog ? frameStyleFlagsFromDialog(*dialog) : 0);
        return;
    }
    case Command::ToolAddLineOrBox:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("LINEFRAME"), this, dialogContext());
        if (dialog) {
            initializeLineFrameDialog(*dialog);
        }
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        ReportLineBoxShape shape = ReportLineBoxShape::Box;
        if (dialog && isChecked(*dialog, Control::LineFrameHorizontal)) {
            shape = ReportLineBoxShape::HorizontalLine;
        } else if (dialog && isChecked(*dialog, Control::LineFrameVertical)) {
            shape = ReportLineBoxShape::VerticalLine;
        }
        designer->addLineBoxFrameShape(
            shape,
            dialog ? comboIndex(*dialog, Control::LineFrameLineStyle) : 0,
            dialog ? comboIndex(*dialog, Control::LineFrameFillPattern) : 0,
            dialog ? positiveEditValue(*dialog, Control::LineFrameCornerRadius, 0) : 0);
        return;
    }
    case Command::ToolFrameAttributes:
        designer->selectCurrentFrameText();
        statusBar()->showMessage(tr("Frame attributes are edited in the report designer side panel."), StatusMessageTimeoutMs);
        return;
    case Command::ConfigureDataFont:
    case Command::ConfigureTextFont:
    case Command::ConfigureColors:
        designer->selectCurrentFrameText();
        statusBar()->showMessage(tr("Report typography and color attributes are edited in the report designer side panel."), StatusMessageTimeoutMs);
        return;
    case Command::ToolChangeForm:
    {
        ReportDefinition report = designer->report();
        if (!configureReportForm(&report)) {
            return;
        }
        designer->applyForm(
            report.formType,
            report.formWidth,
            report.formHeight,
            report.rows,
            report.columns,
            report.marginLeft,
            report.marginTop,
            report.marginRight,
            report.marginBottom,
            report.horizontalGutter,
            report.verticalGutter);
        statusBar()->showMessage(tr("Report form changed."), StatusMessageTimeoutMs);
        return;
    }
    default:
        showUiCommandStatus(commandId);
        return;
    }
}

void MainWindow::handleTemplateDesignerCommand(int commandId)
{
    TemplateDesignerWidget* designer = activeTemplateDesigner();
    if (designer == nullptr) {
        showUiCommandStatus(commandId);
        return;
    }

    switch (commandId) {
    case Command::FileSave:
        designer->save();
        return;
    case Command::FileSaveAs:
        exportTemplatePackageFromDesigner(designer);
        return;
    case Command::FileClose:
        if (QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow()) {
            subWindow->close();
        }
        return;
    case Command::EditClear:
        designer->deleteSelectedFrame();
        return;
    case Command::ToolAddText:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TEXTFRAME"), this, dialogContext());
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        designer->addTextFrameWithText(dialog ? editText(*dialog, Control::FrameText) : QString(), dialog ? frameStyleFlagsFromDialog(*dialog) : 0);
        return;
    }
    case Command::ToolAddDataBox:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("DATAFRAME"), this, dialogContext());
        if (dialog) {
            populateComboIfEmpty(*dialog, Control::DataFrameFieldList, designer->fieldNames());
        }
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        QString fieldName;
        if (dialog) {
            fieldName = comboText(*dialog, Control::DataFrameFieldList);
            if (fieldName.isEmpty()) {
                if (auto* edit = uiControl<QLineEdit>(*dialog, Control::FrameText)) {
                    fieldName = edit->text();
                }
            }
        }
        designer->addDataBoxFrameForField(fieldName, dialog ? frameStyleFlagsFromDialog(*dialog) : 0);
        return;
    }
    case Command::ToolAddNotesBox:
        designer->addNotesBoxFrame();
        return;
    case Command::ToolAddLineOrBox:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("LINEFRAME"), this, dialogContext());
        if (dialog) {
            initializeLineFrameDialog(*dialog);
        }
        if (dialog && dialog->exec() != QDialog::Accepted) {
            return;
        }
        CardTemplateLineBoxShape shape = CardTemplateLineBoxShape::Box;
        if (dialog && isChecked(*dialog, Control::LineFrameHorizontal)) {
            shape = CardTemplateLineBoxShape::HorizontalLine;
        } else if (dialog && isChecked(*dialog, Control::LineFrameVertical)) {
            shape = CardTemplateLineBoxShape::VerticalLine;
        }
        designer->addLineBoxFrameShape(
            shape,
            dialog ? comboIndex(*dialog, Control::LineFrameLineStyle) : 0,
            dialog ? comboIndex(*dialog, Control::LineFrameFillPattern) : 0,
            dialog ? positiveEditValue(*dialog, Control::LineFrameCornerRadius, 0) : 0);
        return;
    }
    case Command::ToolFrameAttributes:
        statusBar()->showMessage(tr("Frame attributes are edited in the template designer side panel."), StatusMessageTimeoutMs);
        return;
    case Command::ConfigureDataFont:
    case Command::ConfigureNameFont:
    case Command::ConfigureTextFont:
    case Command::ConfigureIndexFont:
    case Command::ConfigureColors:
        statusBar()->showMessage(tr("Template typography and color controls are represented by frame styles in the inspector."), StatusMessageTimeoutMs);
        return;
    default:
        showUiCommandStatus(commandId);
        return;
    }
}

void MainWindow::handleSecurityCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    const QString dialogName = workspace->hasSecurity() ? QStringLiteral("REMOVESECURITY") : QStringLiteral("ADDSECURITY");
    while (true) {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, this, dialogContext());
        if (!dialog) {
            statusBar()->showMessage(tr("Security dialog is not available."), StatusMessageTimeoutMs);
            return;
        }

        if (dialog->exec() != QDialog::Accepted) {
            return;
        }

        const QString password = normalizedSecurityPassword(*dialog);
        if (!workspace->hasSecurity()) {
            if (password.isEmpty()) {
                QMessageBox::warning(this, tr("CardStack"), tr("Password cannot be empty."));
                continue;
            }
            if (password.contains(QLatin1Char(' '))) {
                QMessageBox::warning(this, tr("CardStack"), tr("Password cannot contain any spaces"));
                continue;
            }
            if (!verifyNewSecurityPassword(password)) {
                continue;
            }

            workspace->setSecurity(password, isChecked(*dialog, Control::SecurityEncryptData));
            statusBar()->showMessage(
                workspace->hasEncryptedSecurity()
                    ? tr("Deck security enabled with data encryption.")
                    : tr("Deck security enabled."), StatusMessageTimeoutMs);
            return;
        }

        if (!workspace->securityPasswordMatches(password)) {
            QMessageBox::warning(this, tr("CardStack"), tr("ACCESS DENIED: Wrong Password"));
            continue;
        }

        workspace->clearSecurity();
        statusBar()->showMessage(tr("Deck security removed."), StatusMessageTimeoutMs);
        return;
    }
}

void MainWindow::handleDeckDescriptionCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CHANGEDESC"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Deck description dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    const auto* description = uiControl<QLineEdit>(*dialog, Control::DeckDescriptionText);
    workspace->setDeckDescription(description == nullptr ? QString() : description->text());
    statusBar()->showMessage(tr("Deck description changed."), StatusMessageTimeoutMs);
}

bool MainWindow::verifyNewSecurityPassword(const QString& password)
{
    while (true) {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("VERIFYPASSWORD"), this, dialogContext());
        if (!dialog) {
            return true;
        }

        if (dialog->exec() != QDialog::Accepted) {
            return false;
        }

        if (normalizedSecurityPassword(*dialog) == password) {
            return true;
        }

        QMessageBox::warning(this, tr("CardStack"), tr("ACCESS DENIED: Wrong Password"));
    }
}

QString MainWindow::normalizedSecurityPassword(const QDialog& dialog) const
{
    const auto* password = uiControl<QLineEdit>(dialog, Control::SecurityPassword);
    return password == nullptr ? QString() : password->text().left(8).toUpper();
}

std::optional<QString> MainWindow::promptLegacyDeckPassword(const QString& filePath)
{
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("GETPASSWORD"), this, dialogContext());
    if (!dialog) {
        QMessageBox::warning(this, tr("CardStack Migration"), tr("Legacy password dialog is not available."));
        return std::nullopt;
    }

    if (auto* fileName = uiControl<QLabel>(*dialog, Control::SecurityFileName)) {
        fileName->setText(QFileInfo(filePath).fileName());
    }

    if (dialog->exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return normalizedSecurityPassword(*dialog);
}

void MainWindow::showUiCommandStatus(int commandId)
{
    statusBar()->showMessage(tr("Command %1 is not implemented yet.").arg(commandId), StatusMessageTimeoutMs);
}

QString MainWindow::chooseOpenPath()
{
    QFileDialog dialog(this, tr("Open"));
    dialog.setObjectName(QStringLiteral("cardstackOpenDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({
        tr("CardStack decks (*.cardstack *.csdeck)"),
        tr("CardStack templates (*.cstemplate)"),
        tr("CardStack reports (*.csreport)"),
        tr("Legacy reports (*.rpt)"),
        tr("Delimited text (*.csv *.tsv *.tab)"),
        tr("Legacy Btrieve decks (*.btn *.btr *.dat)"),
        tr("Legacy interchange (*.dbf *.crd *.wp *.tn)"),
        tr("All files (*.*)"),
    });
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString MainWindow::chooseMergeSourcePath()
{
    QFileDialog dialog(this, tr("Merge"));
    dialog.setObjectName(QStringLiteral("cardstackMergeSourceDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({
        tr("CardStack decks (*.cardstack *.csdeck)"),
        tr("Legacy Btrieve decks (*.btn *.btr *.dat)"),
        tr("All files (*.*)"),
    });
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString MainWindow::chooseSaveDeckPath(const QString& suggestedPath)
{
    QFileDialog dialog(this, tr("Save As"));
    dialog.setObjectName(QStringLiteral("cardstackSaveDeckDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({
        tr("CardStack decks (*.cardstack *.csdeck)"),
        tr("All files (*.*)"),
    });
    if (!suggestedPath.isEmpty()) {
        dialog.selectFile(suggestedPath);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString MainWindow::chooseExportTemplatePath(const QString& suggestedPath)
{
    QFileDialog dialog(this, tr("Export Template"));
    dialog.setObjectName(QStringLiteral("cardstackExportTemplateDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({
        tr("CardStack templates (*.cstemplate)"),
        tr("All files (*.*)"),
    });
    if (!suggestedPath.isEmpty()) {
        dialog.selectFile(suggestedPath);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString MainWindow::chooseExportReportPath(const QString& suggestedPath)
{
    QFileDialog dialog(this, tr("Export Report"));
    dialog.setObjectName(QStringLiteral("cardstackExportReportDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({
        tr("CardStack reports (*.csreport)"),
        tr("All files (*.*)"),
    });
    if (!suggestedPath.isEmpty()) {
        dialog.selectFile(suggestedPath);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

void MainWindow::showHelpContents()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("CardStack Help"));
    dialog.setWindowIcon(windowIcon().isNull() ? cardStackIcon() : windowIcon());
    dialog.resize(HtmlDialogWidthPx, HtmlDialogHeightPx);

    auto* layout = new QVBoxLayout(&dialog);
    auto* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);

    QFile helpFile(QStringLiteral(":/cardstack/help/index.html"));
    if (helpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        browser->setHtml(QString::fromUtf8(helpFile.readAll()));
    } else {
        browser->setHtml(tr("<h1>CardStack Help</h1><p>Help content could not be loaded.</p>"));
    }
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showAboutDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About CardStack"));
    dialog.setWindowIcon(windowIcon());

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto* logo = new QLabel(&dialog);
    logo->setAlignment(Qt::AlignCenter);
    const QPixmap logoPixmap(QStringLiteral(":/cardstack/logo-wide.png"));
    if (!logoPixmap.isNull()) {
        logo->setPixmap(logoPixmap.scaled(520, 208, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logo->setText(tr("CardStack"));
    }
    layout->addWidget(logo);

    auto* text = new QLabel(
        tr("<b>CardStack</b><br>"
           "A modern open-source card database built with Qt and SQLite.<br><br>"
           "License: GPLv3-or-later."),
        &dialog);
    text->setAlignment(Qt::AlignCenter);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::handlePhoneDialCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    QStringList candidates;
    if (workspace != nullptr) {
        candidates = phoneCandidatesForWorkspace(*workspace);
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CALL"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Phone dial dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    auto* numberEdit = uiControl<QLineEdit>(*dialog, Control::PhoneNumber);
    auto* cardNumbers = uiControl<QListWidget>(*dialog, Control::PhoneCardNumbers);
    auto* quickDials = uiControl<QListWidget>(*dialog, Control::PhoneQuickDials);
    if (cardNumbers != nullptr) {
        cardNumbers->clear();
        for (const QString& candidate : candidates) {
            auto* item = new QListWidgetItem(candidate, cardNumbers);
            item->setData(Qt::UserRole, phoneValueFromCandidate(candidate));
        }
        connect(cardNumbers, &QListWidget::currentItemChanged, dialog.get(), [numberEdit](QListWidgetItem* current) {
            if (numberEdit != nullptr && current != nullptr) {
                numberEdit->setText(current->data(Qt::UserRole).toString());
            }
        });
        if (cardNumbers->count() > 0) {
            cardNumbers->setCurrentRow(0);
        }
    }

    if (quickDials != nullptr) {
        quickDials->clear();
        for (const QuickDial& quickDial : m_quickDials) {
            auto* item = new QListWidgetItem(quickDialDisplay(quickDial.description, quickDial.phoneNumber), quickDials);
            item->setData(Qt::UserRole, quickDial.phoneNumber);
        }
        connect(quickDials, &QListWidget::currentItemChanged, dialog.get(), [numberEdit](QListWidgetItem* current) {
            if (numberEdit != nullptr && current != nullptr) {
                numberEdit->setText(current->data(Qt::UserRole).toString());
            }
        });
    }

    setChecked(*dialog, Control::PhoneOutsideLine, m_phoneGetOutsideLine);
    setChecked(*dialog, Control::PhoneLongDistance, m_phoneUseLongDistance);
    setChecked(*dialog, Control::PhoneLogCall, m_phoneLogCalls);

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    QString number = numberEdit == nullptr ? QString() : numberEdit->text().trimmed();
    if (isChecked(*dialog, Control::PhoneOutsideLine) && !m_phoneOutsideLinePrefix.trimmed().isEmpty()) {
        number.prepend(m_phoneOutsideLinePrefix.trimmed());
    }
    if (isChecked(*dialog, Control::PhoneLongDistance) && !m_phoneLongDistancePrefix.trimmed().isEmpty()) {
        number.prepend(m_phoneLongDistancePrefix.trimmed());
    }

    const QString dialString = normalizedPhoneDialString(number);
    if (dialString.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Dial Phone Number"),
            tr("Enter a phone number before dialing."));
        return;
    }

    QApplication::clipboard()->setText(number);
    const bool opened = QDesktopServices::openUrl(QUrl(QStringLiteral("tel:%1").arg(dialString)));
    if (opened) {
        statusBar()->showMessage(tr("Phone number copied and sent to the system phone handler."), StatusMessageTimeoutMs);
    } else {
        QMessageBox::information(
            this,
            tr("Dial Phone Number"),
            tr("The phone number was copied to the clipboard, but no system phone handler accepted the tel: link."));
    }
}

void MainWindow::handlePhoneDialerConfigCommand()
{
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("PHNDEF"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Phone dialer configuration dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    auto* quickDials = uiControl<QListWidget>(*dialog, Control::PhoneQuickDials);
    const auto refreshQuickDials = [this, quickDials]() {
        if (quickDials == nullptr) {
            return;
        }
        quickDials->clear();
        for (const QuickDial& quickDial : m_quickDials) {
            quickDials->addItem(quickDialDisplay(quickDial.description, quickDial.phoneNumber));
        }
        if (quickDials->count() > 0 && quickDials->currentRow() < 0) {
            quickDials->setCurrentRow(0);
        }
    };
    const auto editQuickDial = [this](const QuickDial& initial, QuickDial* result) {
        std::unique_ptr<QDialog> quickDialog = UiBuilder::createDialog(QStringLiteral("QUICKDIAL"), this, dialogContext());
        if (!quickDialog) {
            return false;
        }
        setEditText(*quickDialog, Control::QuickDialDescription, initial.description);
        setEditText(*quickDialog, Control::QuickDialNumber, initial.phoneNumber);
        if (quickDialog->exec() != QDialog::Accepted) {
            return false;
        }
        QuickDial updated{
            editText(*quickDialog, Control::QuickDialDescription).trimmed(),
            editText(*quickDialog, Control::QuickDialNumber).trimmed(),
        };
        if (updated.phoneNumber.isEmpty()) {
            QMessageBox::information(this, tr("Phone Dialer"), tr("Enter a phone number for the quick dial."));
            return false;
        }
        if (updated.description.isEmpty()) {
            updated.description = updated.phoneNumber;
        }
        *result = updated;
        return true;
    };

    setChecked(*dialog, Control::PhoneLongDistance, m_phoneUseLongDistance);
    setChecked(*dialog, Control::PhoneOutsideLine, m_phoneGetOutsideLine);
    setChecked(*dialog, Control::PhoneLogCall, m_phoneLogCalls);
    setEditText(*dialog, Control::PhoneLongDistancePrefix, m_phoneLongDistancePrefix);
    setEditText(*dialog, Control::PhoneOutsideLinePrefix, m_phoneOutsideLinePrefix);
    setEditText(*dialog, Control::PhoneLocalAreaCode, m_phoneLocalAreaCode);
    refreshQuickDials();

    if (auto* add = uiControl<QAbstractButton>(*dialog, Control::PhoneQuickDialAdd)) {
        connect(add, &QAbstractButton::clicked, dialog.get(), [this, &refreshQuickDials, editQuickDial]() {
            QuickDial quickDial;
            if (editQuickDial({}, &quickDial)) {
                m_quickDials.append(quickDial);
                refreshQuickDials();
            }
        });
    }
    if (auto* modify = uiControl<QAbstractButton>(*dialog, Control::PhoneQuickDialModify)) {
        connect(modify, &QAbstractButton::clicked, dialog.get(), [this, quickDials, &refreshQuickDials, editQuickDial]() {
            if (quickDials == nullptr || quickDials->currentRow() < 0 || quickDials->currentRow() >= m_quickDials.size()) {
                return;
            }
            QuickDial quickDial;
            if (editQuickDial(m_quickDials.at(quickDials->currentRow()), &quickDial)) {
                m_quickDials[quickDials->currentRow()] = quickDial;
                refreshQuickDials();
            }
        });
    }
    if (auto* remove = uiControl<QAbstractButton>(*dialog, Control::PhoneQuickDialDelete)) {
        connect(remove, &QAbstractButton::clicked, dialog.get(), [this, quickDials, &refreshQuickDials]() {
            if (quickDials == nullptr || quickDials->currentRow() < 0 || quickDials->currentRow() >= m_quickDials.size()) {
                return;
            }
            m_quickDials.removeAt(quickDials->currentRow());
            refreshQuickDials();
        });
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    m_phoneUseLongDistance = isChecked(*dialog, Control::PhoneLongDistance);
    m_phoneGetOutsideLine = isChecked(*dialog, Control::PhoneOutsideLine);
    m_phoneLogCalls = isChecked(*dialog, Control::PhoneLogCall);
    m_phoneLongDistancePrefix = editText(*dialog, Control::PhoneLongDistancePrefix).trimmed();
    m_phoneOutsideLinePrefix = editText(*dialog, Control::PhoneOutsideLinePrefix).trimmed();
    m_phoneLocalAreaCode = editText(*dialog, Control::PhoneLocalAreaCode).trimmed();
    statusBar()->showMessage(tr("Phone dialer settings updated."), StatusMessageTimeoutMs);
}

int MainWindow::showUiDialog(const QString& dialogName)
{
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Dialog %1 is not available.").arg(dialogName), StatusMessageTimeoutMs);
        return QDialog::Rejected;
    }

    return dialog->exec();
}

bool MainWindow::configureReportForm(ReportDefinition* report)
{
    if (report == nullptr) {
        return false;
    }

    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("REPORTFORM"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("Forms dialog is not available."), StatusMessageTimeoutMs);
        return false;
    }

    ReportFormType selectedType = report->formType;
    if (selectedType != ReportFormType::Card && selectedType != ReportFormType::Label) {
        selectedType = ReportFormType::Report;
    }

    auto* reportFormGroup = new QButtonGroup(dialog.get());
    reportFormGroup->setExclusive(true);
    for (int controlId : {Control::ReportFormCard, Control::ReportFormLabel, Control::ReportFormReport}) {
        if (auto* button = uiControl<QAbstractButton>(*dialog, controlId)) {
            reportFormGroup->addButton(button, controlId);
        }
    }

    auto* list = uiControl<QListWidget>(*dialog, Control::ReportFormList);
    const auto fitChooseFormGroup = [dialog = dialog.get(), list]() {
        if (list == nullptr) {
            return;
        }
        const QList<QGroupBox*> groups = dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
        for (QGroupBox* group : groups) {
            if (!group->title().contains(QStringLiteral("Choose a form"), Qt::CaseInsensitive)) {
                continue;
            }
            QRect geometry = group->geometry();
            geometry.setBottom(std::max(geometry.bottom(), list->geometry().bottom() + 8));
            group->setGeometry(geometry);
        }
    };
    if (list != nullptr) {
        QRect listGeometry = list->geometry();
        listGeometry.setHeight(std::max(listGeometry.height(), ReportFormListMinimumHeightPx));
        list->setGeometry(listGeometry);
        fitChooseFormGroup();
        const int requiredHeight = listGeometry.bottom() + ReportFormDialogBottomPaddingPx;
        if (dialog->height() < requiredHeight) {
            dialog->resize(dialog->width(), requiredHeight);
            dialog->setMinimumHeight(requiredHeight);
        }
    }
    const auto populateList = [this, list, dialog = dialog.get()](ReportFormType type) {
        if (list == nullptr) {
            return;
        }
        list->clear();
        const QVector<ReportFormPreset> presets = reportFormPresets(type);
        for (int index = 0; index < presets.size(); ++index) {
            auto* item = new QListWidgetItem(tr(presets.at(index).label), list);
            item->setData(Qt::UserRole, index);
        }
        if (list->count() > 0) {
            list->setCurrentRow(0);
        }
        const int rowHeight = std::max(1, list->sizeHintForRow(0));
        const int listHeight = std::max(
            ReportFormListMinimumHeightPx,
            rowHeight * std::max(1, list->count()) + 2 * list->frameWidth() + 6);
        QRect listGeometry = list->geometry();
        listGeometry.setHeight(listHeight);
        list->setGeometry(listGeometry);
        const QList<QGroupBox*> groups = dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
        for (QGroupBox* group : groups) {
            if (!group->title().contains(QStringLiteral("Choose a form"), Qt::CaseInsensitive)) {
                continue;
            }
            QRect geometry = group->geometry();
            geometry.setBottom(std::max(geometry.bottom(), listGeometry.bottom() + 8));
            group->setGeometry(geometry);
        }

        const int requiredHeight = listGeometry.bottom() + ReportFormDialogBottomPaddingPx;
        if (dialog->height() < requiredHeight) {
            dialog->resize(dialog->width(), requiredHeight);
            dialog->setMinimumHeight(requiredHeight);
        }
    };

    setChecked(*dialog, Control::ReportFormCard, selectedType == ReportFormType::Card);
    setChecked(*dialog, Control::ReportFormLabel, selectedType == ReportFormType::Label);
    setChecked(*dialog, Control::ReportFormReport, selectedType == ReportFormType::Report);
    populateList(selectedType);

    const auto updateType = [&selectedType, populateList](ReportFormType type) {
        selectedType = type;
        populateList(type);
    };
    if (auto* cardButton = uiControl<QAbstractButton>(*dialog, Control::ReportFormCard)) {
        connect(cardButton, &QAbstractButton::clicked, dialog.get(), [updateType]() {
            updateType(ReportFormType::Card);
        });
    }
    if (auto* labelButton = uiControl<QAbstractButton>(*dialog, Control::ReportFormLabel)) {
        connect(labelButton, &QAbstractButton::clicked, dialog.get(), [updateType]() {
            updateType(ReportFormType::Label);
        });
    }
    if (auto* reportButton = uiControl<QAbstractButton>(*dialog, Control::ReportFormReport)) {
        connect(reportButton, &QAbstractButton::clicked, dialog.get(), [updateType]() {
            updateType(ReportFormType::Report);
        });
    }

    bool useCustomForm = false;
    if (auto* customButton = uiControl<QAbstractButton>(*dialog, Control::ReportFormCustom)) {
        connect(customButton, &QAbstractButton::clicked, dialog.get(), [&useCustomForm, dialog = dialog.get()]() {
            useCustomForm = true;
            dialog->accept();
        });
    }

    if (dialog->exec() != QDialog::Accepted) {
        return false;
    }

    if (isChecked(*dialog, Control::ReportFormCard)) {
        selectedType = ReportFormType::Card;
    } else if (isChecked(*dialog, Control::ReportFormLabel)) {
        selectedType = ReportFormType::Label;
    } else {
        selectedType = ReportFormType::Report;
    }

    if (useCustomForm) {
        selectedType = ReportFormType::Report;
        std::unique_ptr<QDialog> defineDialog = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"), this, dialogContext());
        if (!defineDialog) {
            statusBar()->showMessage(tr("Define Form dialog is not available."), StatusMessageTimeoutMs);
            return false;
        }

        setEditText(*defineDialog, Control::DefineFormWidth, QString::number(std::max(1, report->formWidth) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormHeight, QString::number(std::max(1, report->formHeight) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginLeft, QString::number(std::max(0, report->marginLeft) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginTop, QString::number(std::max(0, report->marginTop) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginRight, QString::number(std::max(0, report->marginRight) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginBottom, QString::number(std::max(0, report->marginBottom) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormHorizontalGutter, QString::number(std::max(0, report->horizontalGutter) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormVerticalGutter, QString::number(std::max(0, report->verticalGutter) / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormRows, QString::number(std::max(1, report->rows)));
        setEditText(*defineDialog, Control::DefineFormColumns, QString::number(std::max(1, report->columns)));
        const auto setDefineEditWidth = [defineDialog = defineDialog.get()](int controlId, int width) {
            if (auto* edit = uiControl<QLineEdit>(*defineDialog, controlId)) {
                QRect geometry = edit->geometry();
                geometry.setWidth(width);
                edit->setGeometry(geometry);
                edit->setMinimumWidth(width);
                edit->setMaximumWidth(width);
            }
        };
        for (int controlId : {
                 Control::DefineFormHeight,
                 Control::DefineFormWidth,
                 Control::DefineFormMarginTop,
                 Control::DefineFormMarginLeft,
                 Control::DefineFormMarginBottom,
                 Control::DefineFormMarginRight,
                 Control::DefineFormHorizontalGutter,
                 Control::DefineFormVerticalGutter,
             }) {
            setDefineEditWidth(controlId, DefineFormMeasureEditWidthPx);
        }
        setDefineEditWidth(Control::DefineFormRows, DefineFormCountEditWidthPx);
        setDefineEditWidth(Control::DefineFormColumns, DefineFormCountEditWidthPx);

        auto* defineFormGroup = new QButtonGroup(defineDialog.get());
        defineFormGroup->setExclusive(true);
        for (int controlId : {Control::DefineFormCard, Control::DefineFormLabel, Control::DefineFormReport}) {
            if (auto* button = uiControl<QAbstractButton>(*defineDialog, controlId)) {
                defineFormGroup->addButton(button, controlId);
            }
        }
        setChecked(*defineDialog, Control::DefineFormCard, false);
        setChecked(*defineDialog, Control::DefineFormLabel, false);
        setChecked(*defineDialog, Control::DefineFormReport, true);

        auto* orientationGroup = new QButtonGroup(defineDialog.get());
        orientationGroup->setExclusive(true);
        for (int controlId : {Control::DefineFormPortrait, Control::DefineFormLandscape}) {
            if (auto* button = uiControl<QAbstractButton>(*defineDialog, controlId)) {
                orientationGroup->addButton(button, controlId);
            }
        }
        setChecked(*defineDialog, report->formWidth > report->formHeight ? Control::DefineFormLandscape : Control::DefineFormPortrait, true);

        const auto currentDefineType = [defineDialog = defineDialog.get()] {
            if (isChecked(*defineDialog, Control::DefineFormCard)) {
                return ReportFormType::Card;
            }
            if (isChecked(*defineDialog, Control::DefineFormLabel)) {
                return ReportFormType::Label;
            }
            return ReportFormType::Report;
        };
        const auto formTypeName = [this](ReportFormType type) {
            switch (type) {
            case ReportFormType::Card:
                return tr("Card");
            case ReportFormType::Label:
                return tr("Label");
            case ReportFormType::Report:
            default:
                return tr("Report");
            }
        };
        const auto setControlsEnabled = [defineDialog = defineDialog.get()](const QVector<int>& controlIds, bool enabled) {
            for (int controlId : controlIds) {
                if (auto* widget = UiBuilder::controlById(defineDialog, controlId)) {
                    widget->setEnabled(enabled);
                }
            }
        };
        const auto formatInches = [](int mils) {
            return QString::number(std::max(1, mils) / 1000.0, 'f', 2);
        };
        const auto updateDefineFormState = [
            defineDialog = defineDialog.get(),
            currentDefineType,
            formTypeName,
            setControlsEnabled,
            formatInches,
            this
        ] {
            const ReportFormType type = currentDefineType();
            const bool multiCell = type != ReportFormType::Report;
            const QString typeName = formTypeName(type);
            const int widthMils = formMeasureValue(*defineDialog, Control::DefineFormWidth, DefaultReportPageWidthMils);
            const int heightMils = formMeasureValue(*defineDialog, Control::DefineFormHeight, DefaultReportPageHeightMils);
            int rows = positiveEditValue(*defineDialog, Control::DefineFormRows, 1);
            int columns = positiveEditValue(*defineDialog, Control::DefineFormColumns, 1);

            if (auto* groupBox = uiControl<QGroupBox>(*defineDialog, Control::DefineFormCountGroup)) {
                groupBox->setTitle(multiCell
                        ? (type == ReportFormType::Card ? tr("Cards per page") : tr("Labels per page"))
                        : tr("Header / Footer"));
            }
            setLabelText(*defineDialog, Control::DefineFormRowsLabel, multiCell ? tr("Rows:") : tr("Header:"));
            setLabelText(*defineDialog, Control::DefineFormColumnsLabel, multiCell ? tr("Columns:") : tr("Footer:"));
            setLabelText(
                *defineDialog,
                Control::DefineFormComputedWidth,
                tr("%1 width (in): %2").arg(typeName, formatInches(widthMils)));
            setLabelText(
                *defineDialog,
                Control::DefineFormComputedHeight,
                tr("%1 height (in): %2").arg(typeName, formatInches(heightMils)));

            if (!multiCell) {
                rows = 1;
                columns = 1;
                setEditText(*defineDialog, Control::DefineFormRows, QStringLiteral("1"));
                setEditText(*defineDialog, Control::DefineFormColumns, QStringLiteral("1"));
            }
            setControlsEnabled(
                {
                    Control::DefineFormRows,
                    Control::DefineFormColumns,
                    Control::DefineFormHorizontalGutter,
                    Control::DefineFormVerticalGutter,
                },
                multiCell);

            if (auto* sample = UiBuilder::controlById(defineDialog, Control::DefineFormSample)) {
                sample->setProperty("formSample", true);
                sample->setProperty("formSampleRows", std::max(1, rows));
                sample->setProperty("formSampleColumns", std::max(1, columns));
                sample->setProperty("formSampleWidthMils", widthMils);
                sample->setProperty("formSampleHeightMils", heightMils);
                sample->update();
            }
        };

        const auto adjustMeasureEdit = [defineDialog = defineDialog.get()](int editControlId, int deltaHundredths) {
            const int valueMils = formMeasureValue(*defineDialog, editControlId, 0);
            const int adjustedMils = std::max(0, valueMils + deltaHundredths * 10);
            setEditText(*defineDialog, editControlId, QString::number(adjustedMils / 1000.0, 'f', 2));
        };
        const auto adjustCountEdit = [defineDialog = defineDialog.get()](int editControlId, int delta) {
            const int value = positiveEditValue(*defineDialog, editControlId, 1);
            setEditText(*defineDialog, editControlId, QString::number(std::clamp(value + delta, 1, 100)));
        };
        const auto connectMicroScroll = [
            defineDialog = defineDialog.get(),
            adjustMeasureEdit,
            adjustCountEdit,
            updateDefineFormState
        ](int spinControlId, int editControlId, bool measure) {
            auto* spinBox = uiControl<QSpinBox>(*defineDialog, spinControlId);
            if (spinBox == nullptr) {
                return;
            }
            spinBox->setRange(-9999, 9999);
            spinBox->setValue(0);
            QObject::connect(spinBox, &QSpinBox::valueChanged, defineDialog, [=](int value) {
                if (value == 0) {
                    return;
                }
                const int previous = spinBox->property("lastValue").toInt();
                const int delta = value - previous;
                spinBox->setProperty("lastValue", value);
                if (measure) {
                    adjustMeasureEdit(editControlId, delta * 5);
                } else {
                    adjustCountEdit(editControlId, delta);
                }
                const QSignalBlocker blocker(spinBox);
                spinBox->setValue(0);
                spinBox->setProperty("lastValue", 0);
                updateDefineFormState();
            });
        };
        connectMicroScroll(Control::DefineFormWidthSpin, Control::DefineFormWidth, true);
        connectMicroScroll(Control::DefineFormHeightSpin, Control::DefineFormHeight, true);
        connectMicroScroll(Control::DefineFormMarginTopSpin, Control::DefineFormMarginTop, true);
        connectMicroScroll(Control::DefineFormMarginLeftSpin, Control::DefineFormMarginLeft, true);
        connectMicroScroll(Control::DefineFormMarginRightSpin, Control::DefineFormMarginRight, true);
        connectMicroScroll(Control::DefineFormMarginBottomSpin, Control::DefineFormMarginBottom, true);
        connectMicroScroll(Control::DefineFormHorizontalGutterSpin, Control::DefineFormHorizontalGutter, true);
        connectMicroScroll(Control::DefineFormVerticalGutterSpin, Control::DefineFormVerticalGutter, true);
        connectMicroScroll(Control::DefineFormRowsSpin, Control::DefineFormRows, false);
        connectMicroScroll(Control::DefineFormColumnsSpin, Control::DefineFormColumns, false);

        for (int controlId : {
                 Control::DefineFormCard,
                 Control::DefineFormLabel,
                 Control::DefineFormReport,
                 Control::DefineFormPortrait,
                 Control::DefineFormLandscape,
             }) {
            if (auto* button = uiControl<QAbstractButton>(*defineDialog, controlId)) {
                connect(button, &QAbstractButton::clicked, defineDialog.get(), updateDefineFormState);
            }
        }
        for (int controlId : {
                 Control::DefineFormHeight,
                 Control::DefineFormWidth,
                 Control::DefineFormMarginTop,
                 Control::DefineFormMarginLeft,
                 Control::DefineFormMarginBottom,
                 Control::DefineFormMarginRight,
                 Control::DefineFormHorizontalGutter,
                 Control::DefineFormVerticalGutter,
                 Control::DefineFormRows,
                 Control::DefineFormColumns,
            }) {
            if (auto* edit = uiControl<QLineEdit>(*defineDialog, controlId)) {
                connect(edit, &QLineEdit::editingFinished, defineDialog.get(), updateDefineFormState);
                connect(edit, &QLineEdit::textChanged, defineDialog.get(), updateDefineFormState);
            }
        }
        updateDefineFormState();

        if (defineDialog->exec() != QDialog::Accepted) {
            return false;
        }

        ReportFormPreset preset;
        preset.type = ReportFormType::Report;
        if (isChecked(*defineDialog, Control::DefineFormCard)) {
            preset.type = ReportFormType::Card;
        } else if (isChecked(*defineDialog, Control::DefineFormLabel)) {
            preset.type = ReportFormType::Label;
        }
        preset.formWidth = formMeasureValue(*defineDialog, Control::DefineFormWidth, DefaultReportPageWidthMils);
        preset.formHeight = formMeasureValue(*defineDialog, Control::DefineFormHeight, DefaultReportPageHeightMils);
        if (isChecked(*defineDialog, Control::DefineFormLandscape) && preset.formHeight > preset.formWidth) {
            std::swap(preset.formWidth, preset.formHeight);
        }
        preset.rows = positiveEditValue(*defineDialog, Control::DefineFormRows, preset.type == ReportFormType::Label ? DefaultLabelRows : 1);
        preset.columns = positiveEditValue(*defineDialog, Control::DefineFormColumns, preset.type == ReportFormType::Label ? DefaultLabelColumns : 1);
        preset.marginLeft = formMeasureValue(*defineDialog, Control::DefineFormMarginLeft, 0);
        preset.marginTop = formMeasureValue(*defineDialog, Control::DefineFormMarginTop, 0);
        preset.marginRight = formMeasureValue(*defineDialog, Control::DefineFormMarginRight, 0);
        preset.marginBottom = formMeasureValue(*defineDialog, Control::DefineFormMarginBottom, 0);
        preset.horizontalGutter = formMeasureValue(*defineDialog, Control::DefineFormHorizontalGutter, 0);
        preset.verticalGutter = formMeasureValue(*defineDialog, Control::DefineFormVerticalGutter, 0);
        applyReportFormPreset(report, preset);
        return true;
    }

    const QVector<ReportFormPreset> presets = reportFormPresets(selectedType);
    int presetIndex = 0;
    if (list != nullptr && list->currentItem() != nullptr) {
        presetIndex = list->currentItem()->data(Qt::UserRole).toInt();
    }
    if (presetIndex < 0 || presetIndex >= presets.size()) {
        presetIndex = 0;
    }
    if (presets.isEmpty()) {
        return false;
    }

    applyReportFormPreset(report, presets.at(presetIndex));
    return true;
}

void MainWindow::handleNewDeckCommand()
{
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("NEWFILE"), this, dialogContext());
    if (!dialog) {
        statusBar()->showMessage(tr("New File dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    const auto* sourceCombo = uiControl<QComboBox>(*dialog, Control::NewFileSourceCombo);
    const auto* templateList = uiControl<QListWidget>(*dialog, Control::NewFileTemplateList);
    const int sourceIndex = sourceCombo == nullptr ? 0 : sourceCombo->currentIndex();
    QString templateName;
    if (templateList != nullptr && templateList->currentItem() != nullptr) {
        templateName = templateList->currentItem()->text();
    }
    if (templateName.trimmed().isEmpty()) {
        const QStringList templateNames = builtInDeckTemplateNames();
        if (!templateNames.isEmpty()) {
            templateName = templateNames.first();
        }
    }

    switch (sourceIndex) {
    case 0:
        openDeckWindow(createDeckFromTemplateName(templateName), {}, true);
        return;
    case 1:
        openTemplateDesignerForNewDeck(createDeckFromScratch());
        return;
    case 2:
        openTemplateDesignerForNewDeck(createDeckFromTemplateName(templateName, tr("%1 Deck").arg(templateName)));
        return;
    case 3:
    {
        QFileDialog patternDialog(this, tr("Select Deck To Pattern After"));
        patternDialog.setObjectName(QStringLiteral("cardstackPatternDeckDialog"));
        patternDialog.setOption(QFileDialog::DontUseNativeDialog, true);
        patternDialog.setFileMode(QFileDialog::ExistingFile);
        patternDialog.setAcceptMode(QFileDialog::AcceptOpen);
        patternDialog.setNameFilters({
            tr("CardStack decks (*.cardstack *.csdeck)"),
            tr("Legacy Btrieve decks (*.btn *.btr *.dat)"),
            tr("All files (*.*)"),
        });
        if (patternDialog.exec() != QDialog::Accepted) {
            return;
        }

        const QStringList files = patternDialog.selectedFiles();
        if (files.isEmpty()) {
            return;
        }

        Deck patternDeck;
        QString error;
        if (!loadDeckFromPath(files.first(), &patternDeck, &error)) {
            QMessageBox::critical(this, tr("New Card Deck"), tr("Could not load deck to pattern after:\n%1").arg(error));
            return;
        }
        openTemplateDesignerForNewDeck(createDeckPatternedAfterDeck(patternDeck));
        return;
    }
    default:
        openDeckWindow(createDeckFromScratch(), {}, true);
        return;
    }
}

QString MainWindow::dialogNameForCommand(int commandId) const
{
    switch (commandId) {
    default:
        return {};
    }
}

QAction* MainWindow::findUiAction(int commandId) const
{
    const std::function<QAction*(const QList<QAction*>&)> findInActions =
        [&](const QList<QAction*>& actions) -> QAction* {
        for (QAction* action : actions) {
            if (action->data().toInt() == commandId) {
                return action;
            }
            if (QMenu* menu = action->menu()) {
                if (QAction* found = findInActions(menu->actions())) {
                    return found;
                }
            }
        }
        return nullptr;
    };

    if (m_buttonBar != nullptr) {
        if (QAction* action = findInActions(m_buttonBar->actions())) {
            return action;
        }
    }

    return findInActions(menuBar()->actions());
}

DeckWorkspace* MainWindow::activeDeckWorkspace() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
    if (subWindow == nullptr) {
        return nullptr;
    }

    return qobject_cast<DeckWorkspace*>(subWindow->widget());
}

ReportDesignerWidget* MainWindow::activeReportDesigner() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
    if (subWindow == nullptr) {
        return nullptr;
    }

    return qobject_cast<ReportDesignerWidget*>(subWindow->widget());
}

TemplateDesignerWidget* MainWindow::activeTemplateDesigner() const
{
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
    if (subWindow == nullptr) {
        return nullptr;
    }

    return qobject_cast<TemplateDesignerWidget*>(subWindow->widget());
}

QString MainWindow::activeDeckPath() const
{
    const DeckWorkspace* workspace = activeDeckWorkspace();
    return workspace == nullptr ? QString() : workspace->property("cardstackFilePath").toString();
}

void MainWindow::setActiveDeckPath(const QString& filePath)
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
    if (workspace == nullptr || subWindow == nullptr) {
        return;
    }

    workspace->setProperty("cardstackFilePath", filePath);
    const QString title = filePath.isEmpty()
        ? workspace->deck().name()
        : QFileInfo(filePath).completeBaseName();
    subWindow->setProperty("cardstackBaseTitle", title.isEmpty() ? workspace->deck().name() : title);
    updateDeckWindowTitle(subWindow, workspace);
}

bool MainWindow::openDeckFromPath(const QString& filePath)
{
    Deck deck;
    QString error;
    if (isCardStackTemplatePath(filePath)) {
        if (!SQLitePackageStore::loadTemplatePackage(filePath, &deck, &error)) {
            QMessageBox::critical(this, tr("CardStack Templates"), tr("Could not load template package:\n%1").arg(error));
            return false;
        }
        if (deck.name().isEmpty()) {
            deck.setName(QFileInfo(filePath).completeBaseName());
        }
        openDeckWindow(deck, {}, true);
        statusBar()->showMessage(tr("Opened template %1 as a new unsaved deck.").arg(filePath), StatusMessageTimeoutMs);
        return true;
    }
    if (isCardStackReportPath(filePath)) {
        return importReportPackageFromPath(filePath);
    }
    if (isLegacyReportPath(filePath)) {
        return importLegacyReportStoreFromPath(filePath);
    }
    if (!isCardStackDeckPath(filePath) && !isDelimitedTextPath(filePath) && !isLegacyInterchangePath(filePath)) {
        return probeLegacyDeckFromPath(filePath);
    }

    if (!loadDeckFromPath(filePath, &deck, &error)) {
        QMessageBox::critical(this, tr("CardStack"), tr("Could not load deck:\n%1").arg(error));
        return false;
    }

    if (deck.name().isEmpty()) {
        deck.setName(QFileInfo(filePath).completeBaseName());
    }
    if (isDelimitedTextPath(filePath) || isLegacyInterchangePath(filePath)) {
        openDeckWindow(deck, {}, true);
        statusBar()->showMessage(tr("Imported %1 cards from %2").arg(deck.cardCount()).arg(filePath), StatusMessageTimeoutMs);
        return true;
    }

    openDeckWindow(deck, filePath);
    statusBar()->showMessage(tr("Opened %1").arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::importReportPackageFromPath(const QString& filePath)
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        QMessageBox::information(
            this,
            tr("CardStack Reports"),
            tr("Open or select a deck before importing a report package."));
        return false;
    }

    QVector<ReportDefinition> reports;
    QString packageName;
    QString error;
    if (!SQLitePackageStore::loadReportPackage(filePath, &reports, &packageName, &error)) {
        QMessageBox::critical(this, tr("CardStack Reports"), tr("Could not load report package:\n%1").arg(error));
        return false;
    }

    int imported = 0;
    for (ReportDefinition report : reports) {
        QString baseName = report.name.trimmed();
        if (baseName.isEmpty()) {
            baseName = QFileInfo(filePath).completeBaseName();
        }
        QString candidateName = baseName;
        int suffix = 2;
        while (reportNameExists(workspace, candidateName, -1)) {
            candidateName = tr("%1 (%2)").arg(baseName).arg(suffix++);
        }
        report.name = candidateName;
        if (workspace->saveReportDefinition(-1, report)) {
            ++imported;
        }
    }

    statusBar()->showMessage(
        tr("Imported %1 report design(s) from %2.").arg(imported).arg(packageName.isEmpty() ? filePath : packageName),
        StatusMessageTimeoutMs);
    updateCommandState();
    return imported > 0;
}

bool MainWindow::importLegacyReportStoreFromPath(const QString& filePath)
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        QMessageBox::information(
            this,
            tr("Legacy Reports"),
            tr("Open or select a deck before importing a legacy report store."));
        return false;
    }

    const LegacyReportReader reader;
    const LegacyReportReader::Result result = reader.readFile(filePath);
    if (!result.ok()) {
        QMessageBox::critical(
            this,
            tr("Legacy Reports"),
            tr("Could not import legacy report store:\n%1").arg(result.errorMessage));
        return false;
    }

    int imported = 0;
    for (ReportDefinition report : result.reports) {
        QString baseName = report.name.trimmed();
        if (baseName.isEmpty()) {
            baseName = QFileInfo(filePath).completeBaseName();
        }
        QString candidateName = baseName;
        int suffix = 2;
        while (reportNameExists(workspace, candidateName, -1)) {
            candidateName = tr("%1 (%2)").arg(baseName).arg(suffix++);
        }
        report.name = candidateName;
        if (workspace->saveReportDefinition(-1, report)) {
            ++imported;
        }
    }

    statusBar()->showMessage(
        tr("Imported %1 legacy report design(s) from %2.").arg(imported).arg(filePath),
        StatusMessageTimeoutMs);
    updateCommandState();
    return imported > 0;
}

bool MainWindow::loadDeckFromPath(const QString& filePath, Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Deck output is not available.");
        }
        return false;
    }

    if (isCardStackDeckPath(filePath)) {
        SQLiteDeckStore store;
        QString error;
        if (!store.open(filePath, &error) || !store.loadDeck(deck, &error)) {
            if (errorMessage != nullptr) {
                *errorMessage = error;
            }
            return false;
        }
        while (deck->hasSecurity()) {
            const std::optional<QString> password = promptLegacyDeckPassword(filePath);
            if (!password.has_value()) {
                if (errorMessage != nullptr) {
                    *errorMessage = tr("Opening the deck was canceled because it requires a password.");
                }
                return false;
            }
            if (deck->securityPassword() == password->left(8).toUpper()) {
                break;
            }
            QMessageBox::warning(this, tr("CardStack"), tr("ACCESS DENIED: Wrong Password"));
        }
        if (deck->name().isEmpty()) {
            deck->setName(QFileInfo(filePath).completeBaseName());
        }
        return true;
    }

    if (isDelimitedTextPath(filePath)) {
        const ImportExportProfile profile = delimitedTextProfileForPath(filePath, ImportExportProfileType::Import);
        QString error;
        if (!DelimitedText::readDeckFile(filePath, profile, deck, &error)) {
            if (errorMessage != nullptr) {
                *errorMessage = error;
            }
            return false;
        }
        return true;
    }

    if (isLegacyInterchangePath(filePath)) {
        const LegacyInterchangeReader reader;
        LegacyInterchangeReader::Result result = reader.readFile(filePath);
        while (result.passwordRequired || result.passwordRejected) {
            const std::optional<QString> password = promptLegacyDeckPassword(filePath);
            if (!password.has_value()) {
                if (errorMessage != nullptr) {
                    *errorMessage = tr("Legacy interchange import was canceled because the file requires a password.");
                }
                return false;
            }

            result = reader.readFile(filePath, *password);
            if (result.passwordRejected) {
                QMessageBox::warning(this, tr("CardStack Migration"), tr("ACCESS DENIED: Wrong Password"));
            }
        }
        if (!result.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage = result.errorMessage;
            }
            return false;
        }

        *deck = result.deck;
        return true;
    }

    LegacyDeckReader::Result result = readLegacyDeckFromPath(filePath);
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    const QString reportPath = legacyReportSidecarPath(filePath);
    if (!reportPath.isEmpty()) {
        const LegacyReportReader reportReader;
        const LegacyReportReader::Result reportResult = reportReader.readFile(reportPath);
        if (reportResult.ok()) {
            for (const ReportDefinition& report : reportResult.reports) {
                result.deck.addReport(report);
            }
        }
    }

    *deck = std::move(result.deck);
    return true;
}

LegacyDeckReader::Result MainWindow::readLegacyDeckFromPath(const QString& filePath)
{
    const LegacyDeckReader reader;
    LegacyDeckReader::Result result = reader.readDeck(filePath);

    while (result.passwordRequired || result.passwordRejected) {
        const std::optional<QString> password = promptLegacyDeckPassword(filePath);
        if (!password.has_value()) {
            result.passwordRequired = false;
            result.errorMessage = tr("Legacy deck import was canceled because the file requires a password.");
            return result;
        }

        result = reader.readDeck(filePath, *password);
        if (result.passwordRejected) {
            QMessageBox::warning(this, tr("CardStack Migration"), tr("ACCESS DENIED: Wrong Password"));
        }
    }

    if (result.ok() && result.legacyPasswordVerificationUnavailable) {
        const QString message = result.legacyDataEncrypted
            ? tr("This legacy deck is owner-password protected and data-encrypted. CardStack decoded the encrypted records using the file's legacy Btrieve owner key table, but the raw migration reader cannot yet verify whether the supplied legacy password is correct.")
            : tr("This legacy deck is owner-password protected. CardStack imported the readable records, but the raw Btrieve migration reader cannot yet verify whether the supplied legacy password is correct.");
        QMessageBox::warning(
            this,
            tr("CardStack Migration"),
            message);
    }
    return result;
}

bool MainWindow::probeLegacyDeckFromPath(const QString& filePath)
{
    LegacyDeckReader::Result result = readLegacyDeckFromPath(filePath);
    if (!result.ok()) {
        QMessageBox::critical(
            this,
            tr("CardStack Migration"),
            tr("Could not import legacy Btrieve deck:\n%1").arg(result.errorMessage));
        return false;
    }

    qsizetype shortest = 0;
    qsizetype longest = 0;
    if (!result.rawRecords.isEmpty()) {
        shortest = result.rawRecords.first().size();
        longest = shortest;
        for (const QByteArray& record : result.rawRecords) {
            shortest = std::min(shortest, record.size());
            longest = std::max(longest, record.size());
        }
    }

    QString reportStatus;
    const QString reportPath = legacyReportSidecarPath(filePath);
    if (!reportPath.isEmpty()) {
        const LegacyReportReader reportReader;
        const LegacyReportReader::Result reportResult = reportReader.readFile(reportPath);
        if (reportResult.ok()) {
            for (const ReportDefinition& report : reportResult.reports) {
                result.deck.addReport(report);
            }
            reportStatus = tr("\nReport designs: %1").arg(reportResult.reports.size());
        } else {
            reportStatus = tr("\nReport designs: not imported (%1)").arg(reportResult.errorMessage);
        }
    } else {
        reportStatus = tr("\nReport designs: no same-name .RPT sidecar found");
    }

    openDeckWindow(result.deck, {}, true);
    QMessageBox::information(
        this,
        tr("CardStack Migration"),
        tr("Legacy Btrieve import succeeded.\n\nCards: %1\nFields: %2\nBtrieve records: %3\nBtrieve page size: %4\nRecord bytes: %5-%6%7")
            .arg(result.deck.cardCount())
            .arg(result.deck.fieldCount())
            .arg(result.rawRecords.size())
            .arg(result.btrieveMetadata.pageSize)
            .arg(shortest)
            .arg(longest)
            .arg(reportStatus));
    statusBar()->showMessage(tr("Imported %1 cards from %2").arg(result.deck.cardCount()).arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::saveActiveDeck()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return false;
    }

    QString filePath = activeDeckPath();
    if (filePath.isEmpty()) {
        return saveActiveDeckAs();
    }

    workspace->commitPendingEdits();
    QString error;
    if (!SQLitePackageStore::saveDeckPackage(workspace->deck(), filePath, &error)) {
        QMessageBox::critical(this, tr("CardStack"), tr("Could not save deck:\n%1").arg(error));
        return false;
    }

    workspace->clearDirty();
    if (QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow()) {
        updateDeckWindowTitle(subWindow, workspace);
    }
    statusBar()->showMessage(tr("Saved %1").arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::saveActiveDeckAs()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return false;
    }

    QString suggestedPath = activeDeckPath();
    if (suggestedPath.isEmpty()) {
        suggestedPath = workspace->deck().name();
    }

    const QString chosenPath = chooseSaveDeckPath(suggestedPath);
    if (chosenPath.isEmpty()) {
        return false;
    }

    const QString filePath = pathWithDefaultDeckSuffix(chosenPath);
    workspace->commitPendingEdits();
    QString error;
    if (!SQLitePackageStore::saveDeckPackage(workspace->deck(), filePath, &error)) {
        QMessageBox::critical(this, tr("CardStack"), tr("Could not save deck:\n%1").arg(error));
        return false;
    }

    setActiveDeckPath(filePath);
    workspace->clearDirty();
    if (QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow()) {
        updateDeckWindowTitle(subWindow, workspace);
    }
    statusBar()->showMessage(tr("Saved %1").arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::exportTemplatePackageFromDesigner(TemplateDesignerWidget* designer)
{
    if (designer == nullptr) {
        return false;
    }

    auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
    if (owner == nullptr) {
        QMessageBox::warning(this, tr("CardStack Templates"), tr("The source deck window is no longer available."));
        return false;
    }

    QString suggestedPath = owner->deck().name();
    if (suggestedPath.trimmed().isEmpty()) {
        suggestedPath = tr("template");
    }

    const QString chosenPath = chooseExportTemplatePath(suggestedPath);
    if (chosenPath.isEmpty()) {
        return false;
    }

    Deck templateDeck = owner->deck();
    templateDeck.setCardTemplateLayout(designer->layoutDefinition());

    QString error;
    const QString filePath = pathWithDefaultTemplateSuffix(chosenPath);
    if (!SQLitePackageStore::saveTemplatePackage(templateDeck, filePath, &error)) {
        QMessageBox::critical(this, tr("CardStack Templates"), tr("Could not export template package:\n%1").arg(error));
        return false;
    }

    statusBar()->showMessage(tr("Exported template %1").arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::exportReportPackageFromDesigner(ReportDesignerWidget* designer)
{
    if (designer == nullptr) {
        return false;
    }

    designer->commitPendingEdits();
    ReportDefinition report = designer->report();
    if (report.name.trimmed().isEmpty()) {
        report.name = tr("Untitled Report");
    }

    const QString chosenPath = chooseExportReportPath(report.name);
    if (chosenPath.isEmpty()) {
        return false;
    }

    QString error;
    const QString filePath = pathWithDefaultReportSuffix(chosenPath);
    if (!SQLitePackageStore::saveReportPackage({report}, report.name, filePath, &error)) {
        QMessageBox::critical(this, tr("CardStack Reports"), tr("Could not export report package:\n%1").arg(error));
        return false;
    }

    statusBar()->showMessage(tr("Exported report %1").arg(filePath), StatusMessageTimeoutMs);
    return true;
}

bool MainWindow::confirmCloseDeckWindow(QMdiSubWindow* subWindow)
{
    if (subWindow == nullptr) {
        return true;
    }

    auto* workspace = qobject_cast<DeckWorkspace*>(subWindow->widget());
    if (workspace == nullptr) {
        return true;
    }

    workspace->commitPendingEdits();
    if (!workspace->isDirty()) {
        return true;
    }

    m_mdiArea->setActiveSubWindow(subWindow);
    const QString title = subWindow->property("cardstackBaseTitle").toString();
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("Save Changes"),
        tr("Save changes to %1 before closing?").arg(title.isEmpty() ? workspace->deck().name() : title),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        return saveActiveDeck();
    }
    return true;
}

bool MainWindow::closeAllSubWindowsWithPrompts()
{
    const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList(QMdiArea::CreationOrder);
    for (QMdiSubWindow* window : windows) {
        if (window == nullptr) {
            continue;
        }
        QPointer<QMdiSubWindow> guard(window);
        m_mdiArea->setActiveSubWindow(window);
        window->close();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (guard != nullptr && m_mdiArea->subWindowList().contains(guard.data())) {
            return false;
        }
    }
    return true;
}

void MainWindow::updateDeckWindowTitle(QMdiSubWindow* subWindow, const DeckWorkspace* workspace) const
{
    if (subWindow == nullptr || workspace == nullptr) {
        return;
    }

    QString title = subWindow->property("cardstackBaseTitle").toString();
    if (title.trimmed().isEmpty()) {
        title = workspace->deck().name().trimmed().isEmpty() ? tr("Untitled Deck") : workspace->deck().name();
    }
    subWindow->setWindowTitle(workspace->isDirty() ? tr("%1 *").arg(title) : title);
}

UiBuilder::DialogContext MainWindow::dialogContext() const
{
    UiBuilder::DialogContext context;
    context.templateNames = defaultTemplates();
    context.reportNames = defaultReports();
    context.recentSearches = {
        tr("CardStack"),
        tr("Windows"),
    };
    context.recentReplacements = {
        tr("CardStack"),
    };

    if (const DeckWorkspace* workspace = activeDeckWorkspace()) {
        context.deckName = workspace->deck().name();
        context.deckDescription = workspace->deck().description();
        context.fieldNames = workspace->fieldNames();
        context.reportNames = reportNamesFromDeck(workspace->deck());
    } else {
        context.deckName = tr("Untitled");
    }

    return context;
}

DeckWorkspace::SearchRequest MainWindow::searchRequestFromDialog(const QDialog& dialog) const
{
    DeckWorkspace::SearchRequest request;
    request.first.text = comboText(dialog, Control::SearchText);
    request.first.fieldIndex = fieldIndexFromCombo(dialog, Control::SearchAllDataBoxes);
    request.first.type = searchTypeFromCombo(dialog, Control::SearchType);
    request.first.wholeWord = isChecked(dialog, Control::SearchWholeWord);
    request.first.caseSensitive = isChecked(dialog, Control::SearchCaseSensitive);
    request.first.soundsLike = isChecked(dialog, Control::SearchSoundsLike);

    request.second.text = comboText(dialog, Control::SearchSecondText);
    request.second.fieldIndex = fieldIndexFromCombo(dialog, Control::SearchSecondAllDataBoxes);
    request.second.type = searchTypeFromCombo(dialog, Control::SearchSecondType);
    request.second.wholeWord = isChecked(dialog, Control::SearchSecondWholeWord);
    request.second.caseSensitive = isChecked(dialog, Control::SearchSecondCaseSensitive);
    request.second.soundsLike = isChecked(dialog, Control::SearchSecondSoundsLike);

    if (isChecked(dialog, Control::SearchCompareAnd)) {
        request.comparison = DeckWorkspace::SearchComparison::And;
    } else if (isChecked(dialog, Control::SearchCompareOr)) {
        request.comparison = DeckWorkspace::SearchComparison::Or;
    }

    if (isChecked(dialog, Control::SearchDirectionForward)) {
        request.direction = DeckWorkspace::SearchDirection::ForwardFromCurrent;
    } else if (isChecked(dialog, Control::SearchDirectionBackward)) {
        request.direction = DeckWorkspace::SearchDirection::BackwardFromCurrent;
    }

    return request;
}

QVector<DeckWorkspace::SortLevel> MainWindow::sortLevelsFromDialog(const QDialog& dialog) const
{
    const struct {
        int fieldControlId;
        int reverseControlId;
    } controls[] = {
        {Control::SortFieldLevel1, Control::SortReverseLevel1},
        {Control::SortFieldLevel2, Control::SortReverseLevel2},
        {Control::SortFieldLevel3, Control::SortReverseLevel3},
    };

    QVector<DeckWorkspace::SortLevel> levels;
    for (const auto& control : controls) {
        DeckWorkspace::SortLevel level;
        level.fieldIndex = fieldIndexFromCombo(dialog, control.fieldControlId);
        level.descending = isChecked(dialog, control.reverseControlId);
        if (level.fieldIndex >= 0) {
            levels.append(level);
        }
    }

    return levels;
}

void MainWindow::updateCommandState()
{
    updateIndexBarVisibility();
    updateToolbarCardPosition();

    if (QAction* action = findUiAction(Command::ConfigureShowButtonBar)) {
        action->setCheckable(true);
        action->setChecked(m_buttonBar != nullptr && !m_buttonBar->isHidden());
    }
    if (QAction* action = findUiAction(Command::ConfigureEnterWorksLikeTab)) {
        action->setCheckable(true);
        action->setChecked(m_enterWorksLikeTab);
    }

    ReportDesignerWidget* designer = activeReportDesigner();
    TemplateDesignerWidget* templateDesigner = activeTemplateDesigner();
    const bool hasDeck = activeDeckWorkspace() != nullptr || designer != nullptr || templateDesigner != nullptr;
    if (QAction* action = findUiAction(Command::ConfigureAddSecurity)) {
        const DeckWorkspace* workspace = activeDeckWorkspace();
        action->setText(workspace != nullptr && workspace->hasSecurity() ? tr("Remove &Security...") : tr("Add &Security..."));
    }

    const QList<int> deckCommandIds = {
        Command::FileRedefine,
        Command::FileMerge,
        Command::FileSave,
        Command::FileSaveAs,
        Command::FileClose,
        Command::FilePrintReport,
        Command::FileNewReport,
        Command::EditUndo,
        Command::EditCut,
        Command::EditCopy,
        Command::EditPaste,
        Command::EditSmartPaste,
        Command::EditClear,
        Command::CardAdd,
        Command::CardDelete,
        Command::CardDuplicate,
        Command::CardUndelete,
        Command::ViewCard,
        Command::ViewTable,
        Command::SearchFind,
        Command::SearchFindNext,
        Command::SearchReplace,
        Command::ConfigureDataFont,
        Command::ConfigureNameFont,
        Command::ConfigureTextFont,
        Command::ConfigureIndexFont,
        Command::ConfigureColors,
        Command::ConfigureIndex,
        Command::ConfigureAddSecurity,
        Command::ConfigureDeckDescription,
        Command::NavigateFirstCard,
        Command::NavigateLastCard,
        Command::NavigatePreviousWindowful,
        Command::NavigatePreviousCard,
        Command::NavigateNextCard,
        Command::NavigateNextWindowful,
    };
    for (int commandId : deckCommandIds) {
        if (QAction* action = findUiAction(commandId)) {
            action->setEnabled(hasDeck);
        }
    }

    const QList<int> designerToolCommandIds = {
        Command::ToolAddText,
        Command::ToolAddDataBox,
        Command::ToolAddNotesBox,
        Command::ToolAddSystemData,
        Command::ToolAddLineOrBox,
        Command::ToolFrameAttributes,
        Command::ToolChangeForm,
    };
    for (int commandId : designerToolCommandIds) {
        if (QAction* action = findUiAction(commandId)) {
            action->setEnabled(false);
        }
    }

    const DeckWorkspace* workspace = activeDeckWorkspace();
    if (designer != nullptr) {
        const QList<int> reportDesignerCommandIds = {
            Command::FileNewReport,
            Command::FileOpenReport,
            Command::FileCloseDeck,
            Command::FileClose,
            Command::FileSaveAs,
            Command::FileSaveReport,
            Command::FileSaveReportAs,
            Command::EditUndo,
            Command::EditCut,
            Command::EditCopy,
            Command::EditPaste,
            Command::ToolAddText,
            Command::ToolAddDataBox,
            Command::ToolAddSystemData,
            Command::ToolAddLineOrBox,
            Command::ToolFrameAttributes,
            Command::ToolChangeForm,
            Command::ConfigureDataFont,
            Command::ConfigureTextFont,
            Command::ConfigureColors,
        };
        for (int commandId : reportDesignerCommandIds) {
            if (QAction* action = findUiAction(commandId)) {
                action->setEnabled(true);
            }
        }
        if (QAction* action = findUiAction(Command::EditClear)) {
            action->setEnabled(designer->selectedFrameIndex() >= 0);
        }
    }

    if (templateDesigner != nullptr) {
        const QList<int> templateDesignerCommandIds = {
            Command::FileSave,
            Command::FileSaveAs,
            Command::FileClose,
            Command::ToolAddText,
            Command::ToolAddDataBox,
            Command::ToolAddNotesBox,
            Command::ToolAddLineOrBox,
            Command::ToolFrameAttributes,
            Command::ConfigureDataFont,
            Command::ConfigureNameFont,
            Command::ConfigureTextFont,
            Command::ConfigureIndexFont,
            Command::ConfigureColors,
        };
        for (int commandId : templateDesignerCommandIds) {
            if (QAction* action = findUiAction(commandId)) {
                action->setEnabled(true);
            }
        }
        if (QAction* action = findUiAction(Command::EditClear)) {
            action->setEnabled(templateDesigner->selectedFrameIndex() >= 0);
        }
    }

    if (QAction* action = findUiAction(Command::EditUndo)) {
        action->setEnabled(workspace != nullptr && workspace->canUndo());
    }
    if (QAction* action = findUiAction(Command::CardUndelete)) {
        action->setEnabled(workspace != nullptr && workspace->canUndelete());
    }
    if (QAction* action = findUiAction(Command::ViewCard)) {
        action->setCheckable(true);
        action->setChecked(workspace != nullptr && workspace->viewMode() == DeckWorkspace::ViewMode::Card);
        action->setShortcut(workspace != nullptr && workspace->viewMode() == DeckWorkspace::ViewMode::Table
            ? QKeySequence(Qt::Key_F9)
            : QKeySequence());
    }
    if (QAction* action = findUiAction(Command::ViewTable)) {
        action->setCheckable(true);
        action->setChecked(workspace != nullptr && workspace->viewMode() == DeckWorkspace::ViewMode::Table);
        action->setShortcut(workspace != nullptr && workspace->viewMode() == DeckWorkspace::ViewMode::Card
            ? QKeySequence(Qt::Key_F9)
            : QKeySequence());
    }
}

void MainWindow::openSampleDeck()
{
    openDeckWindow(createSampleProjectDeck());
}

DeckWorkspace* MainWindow::openDeckWindow(const Deck& deck, const QString& filePath, bool initialDirty)
{
    auto* workspace = new DeckWorkspace(deck);
    workspace->setProperty("cardstackFilePath", filePath);
    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(workspace);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(windowIcon().isNull() ? cardStackIcon() : windowIcon());
    const QString title = filePath.isEmpty() ? deck.name() : QFileInfo(filePath).completeBaseName();
    subWindow->setProperty("cardstackBaseTitle", title.isEmpty() ? deck.name() : title);
    updateDeckWindowTitle(subWindow, workspace);
    subWindow->resize(860, 420);
    configureSubWindowSystemMenu(subWindow);
    subWindow->show();
    connect(workspace, &DeckWorkspace::dirtyChanged, subWindow, [this, subWindow, workspace](bool) {
        updateDeckWindowTitle(subWindow, workspace);
        updateWindowMenuEntries();
    });
    connect(workspace, &DeckWorkspace::cardPositionChanged, this, [this](int, int) {
        updateToolbarCardPosition();
    });
    connect(subWindow, &QMdiSubWindow::destroyed, this, [this]() {
        updateWindowMenuEntries();
        updateCommandState();
    });
    if (initialDirty) {
        workspace->markDirty();
    }
    refreshMenuForActiveWindow();
    updateCommandState();
    return workspace;
}

void MainWindow::openReportDesigner(DeckWorkspace* workspace, int reportIndex, const ReportDefinition& report)
{
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    auto* designer = new ReportDesignerWidget(report, workspace->fieldNames());
    designer->setProperty("reportIndex", reportIndex);
    designer->setProperty("ownerDeckWorkspace", QVariant::fromValue<QObject*>(workspace));
    designer->setProperty("ownerReportIndex", reportIndex);
    connect(designer, &ReportDesignerWidget::commandRequested, this, [this](int commandId) {
        handleReportDesignerCommand(commandId);
        updateCommandState();
    });

    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(designer);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(windowIcon().isNull() ? cardStackIcon() : windowIcon());
    subWindow->setWindowTitle(report.name.trimmed().isEmpty() ? tr("Untitled Report") : report.name);
    subWindow->resize(ReportDesignerWindowWidthPx, ReportDesignerWindowHeightPx);
    configureSubWindowSystemMenu(subWindow);
    subWindow->show();

    connect(designer, &ReportDesignerWidget::dirtyChanged, subWindow, [this, subWindow, designer](bool dirty) {
        const QString name = designer->report().name.trimmed().isEmpty()
            ? QObject::tr("Untitled Report")
            : designer->report().name;
        subWindow->setWindowTitle(dirty ? QObject::tr("%1 *").arg(name) : name);
        updateWindowMenuEntries();
    });
    connect(designer, &ReportDesignerWidget::saveRequested, this, [this, designer, subWindow](const ReportDefinition& updatedReport) {
        auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
        if (owner == nullptr) {
            QMessageBox::warning(this, tr("CardStack Reports"), tr("The source deck window is no longer available."));
            designer->markDirty();
            return;
        }

        int reportIndex = designer->property("reportIndex").toInt();
        if (reportIndex < 0) {
            reportIndex = owner->deck().reportCount();
        }
        if (!owner->saveReportDefinition(reportIndex, updatedReport)) {
            QMessageBox::warning(this, tr("CardStack Reports"), tr("Could not save the report design."));
            designer->markDirty();
            return;
        }
        designer->setProperty("reportIndex", reportIndex);
        subWindow->setWindowTitle(updatedReport.name);
        updateWindowMenuEntries();
        statusBar()->showMessage(tr("Report design saved."), StatusMessageTimeoutMs);
        updateCommandState();
    });
    refreshMenuForActiveWindow();
    updateCommandState();
}

void MainWindow::openTemplateDesigner(DeckWorkspace* workspace)
{
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    QPointer<QMdiSubWindow> ownerSubWindow;
    for (QMdiSubWindow* candidate : m_mdiArea->subWindowList()) {
        if (candidate->widget() == workspace) {
            ownerSubWindow = candidate;
            break;
        }
    }

    auto* designer = new TemplateDesignerWidget(workspace->deck().cardTemplateLayout(), workspace->deck().fields());
    designer->setProperty("ownerDeckWorkspace", QVariant::fromValue<QObject*>(workspace));
    designer->setProperty("ownerDeckSubWindow", QVariant::fromValue<QObject*>(ownerSubWindow.data()));

    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(designer);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(windowIcon().isNull() ? cardStackIcon() : windowIcon());
    const QString deckName = workspace->deck().name().trimmed().isEmpty()
        ? tr("Untitled Deck")
        : workspace->deck().name();
    subWindow->setWindowTitle(tr("%1 Template Design").arg(deckName));
    subWindow->resize(TemplateDesignerWindowWidthPx, TemplateDesignerWindowHeightPx);
    configureSubWindowSystemMenu(subWindow);
    if (ownerSubWindow != nullptr) {
        ownerSubWindow->hide();
    }
    subWindow->show();

    connect(designer, &TemplateDesignerWidget::dirtyChanged, subWindow, [this, subWindow, deckName](bool dirty) {
        subWindow->setWindowTitle(dirty
            ? QObject::tr("%1 Template Design *").arg(deckName)
            : QObject::tr("%1 Template Design").arg(deckName));
        updateWindowMenuEntries();
    });
    connect(designer, &TemplateDesignerWidget::saveRequested, this, [this, designer](const CardTemplateLayout& layout) {
        auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
        if (owner == nullptr) {
            QMessageBox::warning(this, tr("CardStack Templates"), tr("The source deck window is no longer available."));
            designer->markDirty();
            return;
        }

        owner->setCardTemplateLayout(layout);
        const QString filePath = owner->property("cardstackFilePath").toString();
        if (!filePath.isEmpty()) {
            owner->commitPendingEdits();
            QString error;
            if (!SQLitePackageStore::saveDeckPackage(owner->deck(), filePath, &error)) {
                QMessageBox::critical(this, tr("CardStack"), tr("Could not save deck:\n%1").arg(error));
                designer->markDirty();
                return;
            }

            owner->clearDirty();
            for (QMdiSubWindow* deckWindow : m_mdiArea->subWindowList()) {
                if (deckWindow->widget() == owner) {
                    updateDeckWindowTitle(deckWindow, owner);
                    break;
                }
            }
            statusBar()->showMessage(tr("Template design applied and deck saved."), StatusMessageTimeoutMs);
        } else {
            statusBar()->showMessage(tr("Template design applied to deck. Save the deck to keep it."), StatusMessageTimeoutMs);
        }
        updateWindowMenuEntries();
        updateCommandState();
    });
    connect(subWindow, &QMdiSubWindow::destroyed, this, [this, ownerSubWindow]() {
        if (ownerSubWindow != nullptr) {
            ownerSubWindow->show();
            m_mdiArea->setActiveSubWindow(ownerSubWindow);
        }
        refreshMenuForActiveWindow();
        updateWindowMenuEntries();
        updateCommandState();
    });
    refreshMenuForActiveWindow();
    updateCommandState();
}

void MainWindow::openTemplateDesignerForNewDeck(Deck deck)
{
    auto draftDeck = std::make_shared<Deck>(std::move(deck));
    auto* designer = new TemplateDesignerWidget(draftDeck->cardTemplateLayout(), draftDeck->fields());
    designer->setProperty("draftTemplateDesigner", true);

    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(designer);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(windowIcon().isNull() ? cardStackIcon() : windowIcon());
    const QString deckName = draftDeck->name().trimmed().isEmpty()
        ? tr("Untitled Deck")
        : draftDeck->name();
    subWindow->setWindowTitle(tr("%1 Template Design").arg(deckName));
    subWindow->resize(TemplateDesignerWindowWidthPx, TemplateDesignerWindowHeightPx);
    configureSubWindowSystemMenu(subWindow);
    subWindow->show();

    connect(designer, &TemplateDesignerWidget::dirtyChanged, subWindow, [this, subWindow, deckName](bool dirty) {
        subWindow->setWindowTitle(dirty
            ? QObject::tr("%1 Template Design *").arg(deckName)
            : QObject::tr("%1 Template Design").arg(deckName));
        updateWindowMenuEntries();
    });
    connect(designer, &TemplateDesignerWidget::saveRequested, this, [this, draftDeck](const CardTemplateLayout& layout) {
        draftDeck->setCardTemplateLayout(layout);
        statusBar()->showMessage(tr("Template design saved. Close the designer to create a deck from it."), StatusMessageTimeoutMs);
        updateCommandState();
    });
    connect(subWindow, &QMdiSubWindow::destroyed, this, [this, draftDeck]() {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            tr("New Card Deck"),
            tr("Use this template to create a new deck?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (choice == QMessageBox::Yes) {
            openDeckWindow(*draftDeck, {}, true);
        } else {
            refreshMenuForActiveWindow();
            updateCommandState();
        }
    });
    refreshMenuForActiveWindow();
    updateCommandState();
}

} // namespace CardStack
