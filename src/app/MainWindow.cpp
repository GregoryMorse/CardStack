#include "MainWindow.h"

#include "BuiltInReportTemplates.h"
#include "DelimitedText.h"
#include "PhoneCallLog.h"
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
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEventLoop>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QLocale>
#include <QLineEdit>
#include <QMarginsF>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleOptionButton>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTableView>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <winspool.h>
#endif
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

extern int qInitResources_cardstack_app();

namespace {

constexpr auto WindowSessionGroup = "windowSession";
constexpr auto WindowSessionVersionKey = "version";
constexpr auto WindowSessionMainGeometryKey = "mainGeometry";
constexpr auto WindowSessionMainStateKey = "mainState";
constexpr auto WindowSessionDeckWindowsKey = "deckWindows";
constexpr int CurrentWindowSessionVersion = 1;

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

QIcon cardStackToolbarIcon(const QString& iconName)
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/cardstack/toolbar/%1.svg").arg(iconName));
    icon.addFile(QStringLiteral(":/cardstack/toolbar/png-24/%1.png").arg(iconName), QSize(24, 24));
    icon.addFile(QStringLiteral(":/cardstack/toolbar/png-48/%1.png").arg(iconName), QSize(48, 48));
    return icon;
}

QIcon cardStackWindowIcon(const QString& iconName)
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/cardstack/window-icons/%1.svg").arg(iconName));
    icon.addFile(QStringLiteral(":/cardstack/window-icons/png-24/%1.png").arg(iconName), QSize(24, 24));
    icon.addFile(QStringLiteral(":/cardstack/window-icons/png-48/%1.png").arg(iconName), QSize(48, 48));
    return icon;
}

QIcon deckWindowIcon()
{
    return cardStackWindowIcon(QStringLiteral("deck"));
}

QIcon templateDesignerWindowIcon()
{
    return cardStackWindowIcon(QStringLiteral("template-designer"));
}

QIcon reportDesignerWindowIcon()
{
    return cardStackWindowIcon(QStringLiteral("report-designer"));
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
        if (deck.fieldAt(fieldIndex).isPhone() || fieldNameLooksLikePhone(fieldName) || valueLooksLikePhoneNumber(value)) {
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
constexpr int DefaultReportPaperStyleId = 10551;
constexpr int LabelFormNameFirstId = 10300;
constexpr int LabelFormNameLastId = 10436;
constexpr int CardFormNameFirstId = 10450;
constexpr int CardFormNameLastId = 10510;
constexpr int ReportFormNameFirstId = 10550;
constexpr int ReportFormNameLastId = 10560;
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
constexpr int IndexBarNativeTextPaddingPx = 2;
constexpr int ReportFormListMinimumHeightPx = 190;
constexpr int ReportDesignerWindowWidthPx = 920;
constexpr int ReportDesignerWindowHeightPx = 620;
constexpr int TemplateDesignerWindowWidthPx = 940;
constexpr int TemplateDesignerWindowHeightPx = 620;
constexpr int HtmlDialogWidthPx = 760;
constexpr int HtmlDialogHeightPx = 620;

QFont storedDeckFont(const QString& serialized)
{
    QFont fallback(QStringLiteral("Arial"));
    if (serialized.count(QLatin1Char(',')) < 9) {
        return fallback;
    }
    QFont stored;
    return stored.fromString(serialized) ? stored : fallback;
}

struct ReportFormPreset {
    ReportFormType type = ReportFormType::Report;
    QString label;
    int formWidth = DefaultReportPageWidthMils;
    int formHeight = DefaultReportPageHeightMils;
    int rows = 1;
    int columns = 1;
    int marginLeft = 0;
    int marginTop = 0;
    int marginRight = 0;
    int marginBottom = 0;
    int horizontalGutter = 0;
    int verticalGutter = 0;
    int paperStyleId = DefaultReportPaperStyleId;
    int pageWidth = DefaultReportPageWidthMils;
    int pageHeight = DefaultReportPageHeightMils;
    int orientation = 0;
};

class IndexBarContainer final : public QWidget {
public:
    using QWidget::QWidget;

    QSize sizeHint() const override
    {
        return QSize(1, IndexBarButtonMaxHeightPx + IndexBarVerticalMarginPx * 2);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(0, IndexBarButtonMinHeightPx + IndexBarVerticalMarginPx * 2);
    }
};

class IndexBarButton final : public QPushButton {
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        const QString label = option.text;
        option.text.clear();

        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);

        const bool pressed = option.state.testFlag(QStyle::State_Sunken);
        const int shiftX = pressed ? style()->pixelMetric(QStyle::PM_ButtonShiftHorizontal, &option, this) : 0;
        const int shiftY = pressed ? style()->pixelMetric(QStyle::PM_ButtonShiftVertical, &option, this) : 0;
        const QPalette::ColorGroup colorGroup = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(option.palette.color(colorGroup, QPalette::ButtonText));
        painter.drawText(
            rect().adjusted(1 + shiftX, shiftY, -1 + shiftX, shiftY),
            Qt::AlignCenter | Qt::TextSingleLine,
            label);
    }
};

QVector<ReportFormPreset> reportFormPresets(ReportFormType type)
{
    int firstNameId = ReportFormNameFirstId;
    int lastNameId = ReportFormNameLastId;
    if (type == ReportFormType::Card) {
        firstNameId = CardFormNameFirstId;
        lastNameId = CardFormNameLastId;
    } else if (type == ReportFormType::Label) {
        firstNameId = LabelFormNameFirstId;
        lastNameId = LabelFormNameLastId;
    }

    QVector<ReportFormPreset> presets;
    for (int nameId = firstNameId; nameId <= lastNameId; nameId += 2) {
        const QString label = UiBuilder::resourceString(nameId);
        const QStringList parts = UiBuilder::resourceString(nameId + 1)
                                      .simplified()
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (label.isEmpty() || parts.size() < 10) {
            continue;
        }

        QVector<int> values;
        values.reserve(parts.size());
        bool valid = true;
        for (const QString& part : parts) {
            bool ok = false;
            values.append(part.toInt(&ok));
            valid = valid && ok;
        }
        if (!valid) {
            continue;
        }

        ReportFormPreset preset;
        preset.type = type;
        preset.label = label;
        preset.formHeight = values.at(0);
        preset.formWidth = values.at(1);
        preset.marginLeft = values.at(2);
        preset.marginRight = values.at(3);
        preset.marginTop = values.at(4);
        preset.marginBottom = values.at(5);
        preset.columns = values.at(6);
        preset.rows = values.at(7);
        preset.pageHeight = values.at(8);
        preset.pageWidth = values.at(9);
        preset.orientation = values.value(10);
        preset.horizontalGutter = values.value(11);
        preset.verticalGutter = values.value(12);
        preset.paperStyleId = nameId + 1;
        if (preset.formWidth <= 0 || preset.formHeight <= 0) {
            continue;
        }
        presets.append(std::move(preset));
    }
    return presets;
}

void applyReportFormPreset(ReportDefinition* report, const ReportFormPreset& preset)
{
    if (report == nullptr) {
        return;
    }

    report->formType = preset.type;
    report->formWidth = preset.formWidth;
    report->formHeight = preset.formHeight;
    if (preset.type == ReportFormType::Report) {
        report->headerHeight = report->headerHeight > 0 ? report->headerHeight : 72;
        report->footerHeight = report->footerHeight > 0 ? report->footerHeight : 72;
    } else {
        report->headerHeight = 0;
        report->footerHeight = 0;
    }
    report->rows = preset.rows;
    report->columns = preset.columns;
    report->marginLeft = preset.marginLeft;
    report->marginTop = preset.marginTop;
    report->marginRight = preset.marginRight;
    report->marginBottom = preset.marginBottom;
    report->horizontalGutter = preset.horizontalGutter;
    report->verticalGutter = preset.verticalGutter;
    report->paperStyleId = preset.paperStyleId;
    report->pageWidth = preset.pageWidth;
    report->pageHeight = preset.pageHeight;
    report->orientation = preset.orientation;
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

bool dispatchFocusedTextClipboardCommand(int commandId)
{
    QWidget* focused = QApplication::focusWidget();
    const auto dispatch = [commandId](auto* editor) {
        if (editor == nullptr) {
            return false;
        }
        if (commandId == Command::EditCut) {
            editor->cut();
        } else if (commandId == Command::EditCopy) {
            editor->copy();
        } else if (commandId == Command::EditPaste) {
            editor->paste();
        } else {
            return false;
        }
        return true;
    };
    return dispatch(qobject_cast<QLineEdit*>(focused))
        || dispatch(qobject_cast<QPlainTextEdit*>(focused));
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
        commandId == Command::EditUndo ||
        commandId == Command::EditCut ||
        commandId == Command::EditCopy ||
        commandId == Command::EditPaste ||
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
        commandId == Command::ConfigureColors ||
        commandId == Command::ConfigureDeckDescription;
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

void setComboIndex(QDialog& dialog, int controlId, int index)
{
    if (auto* comboBox = uiControl<QComboBox>(dialog, controlId); comboBox != nullptr && comboBox->count() > 0) {
        comboBox->setCurrentIndex(std::clamp(index, 0, comboBox->count() - 1));
    }
}

void initializeFrameStyleDialog(QDialog& dialog, quint8 styleFlags)
{
    setChecked(dialog, Control::FrameBold, (styleFlags & ReportStyleFlagBold) != 0);
    setChecked(dialog, Control::FrameItalic, (styleFlags & ReportStyleFlagItalic) != 0);
    setChecked(dialog, Control::FrameUnderline, (styleFlags & ReportStyleFlagUnderline) != 0);
    setChecked(dialog, Control::FrameAlignmentRight, (styleFlags & ReportStyleFlagAlignRight) != 0);
    setChecked(dialog, Control::FrameAlignmentCenter, (styleFlags & ReportStyleFlagAlignCenter) != 0);
    setChecked(dialog, Control::FrameAlignmentLeft, (styleFlags & (ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight)) == 0);
    setChecked(dialog, Control::SystemBoxBold, (styleFlags & ReportStyleFlagBold) != 0);
    setChecked(dialog, Control::SystemBoxItalic, (styleFlags & ReportStyleFlagItalic) != 0);
    setChecked(dialog, Control::SystemBoxUnderline, (styleFlags & ReportStyleFlagUnderline) != 0);
    setChecked(dialog, Control::SystemBoxRight, (styleFlags & ReportStyleFlagAlignRight) != 0);
    setChecked(dialog, Control::SystemBoxCenter, (styleFlags & ReportStyleFlagAlignCenter) != 0);
    setChecked(dialog, Control::SystemBoxLeft, (styleFlags & (ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight)) == 0);
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

void initializeLineFrameDialog(QDialog& dialog, int shape, int lineStyle, int fillPattern, int cornerRadius)
{
    initializeLineFrameDialog(dialog);
    setChecked(dialog, Control::LineFrameBox, shape == ReportLineShapeBox);
    setChecked(dialog, Control::LineFrameHorizontal, shape == ReportLineShapeHorizontal);
    setChecked(dialog, Control::LineFrameVertical, shape == ReportLineShapeVertical);
    setComboIndex(dialog, Control::LineFrameLineStyle, lineStyle);
    setComboIndex(dialog, Control::LineFrameFillPattern, fillPattern);
    setEditText(dialog, Control::LineFrameCornerRadius, QString::number(std::max(0, cornerRadius)));
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

void initializeSystemBoxDialogToken(QDialog& dialog, QString token)
{
    token = token.trimmed().toLower();
    if (token.startsWith(QLatin1Char('{')) && token.endsWith(QLatin1Char('}'))) {
        token = token.mid(1, token.size() - 2);
    }
    const QMap<QString, int> dateIndexes = {
        {QStringLiteral("longdate"), 0}, {QStringLiteral("date"), 0}, {QStringLiteral("shortdate"), 1},
        {QStringLiteral("longday"), 2}, {QStringLiteral("shortday"), 3},
        {QStringLiteral("long weekday"), 4}, {QStringLiteral("short weekday"), 5},
        {QStringLiteral("longmonth"), 6}, {QStringLiteral("shortmonth"), 7},
        {QStringLiteral("longyear"), 8}, {QStringLiteral("shortyear"), 9},
        {QStringLiteral("longtime"), 10}, {QStringLiteral("time"), 11},
        {QStringLiteral("hour"), 12}, {QStringLiteral("minutes"), 13}, {QStringLiteral("am/pm"), 14},
    };
    if (dateIndexes.contains(token)) {
        setChecked(dialog, Control::SystemBoxDateCategory, true);
        setComboIndex(dialog, Control::SystemBoxDateFormats, dateIndexes.value(token));
        return;
    }
    if (token == QStringLiteral("page") || token == QStringLiteral("cardtotal")) {
        setChecked(dialog, Control::SystemBoxNumberCategory, true);
        setComboIndex(dialog, Control::SystemBoxNumberFormats, token == QStringLiteral("page") ? 0 : 1);
        return;
    }
    setChecked(dialog, Control::SystemBoxSystemCategory, true);
    const QMap<QString, int> systemIndexes = {
        {QStringLiteral("reportname"), 0}, {QStringLiteral("deckname"), 1},
        {QStringLiteral("description"), 2}, {QStringLiteral("path"), 3},
    };
    setComboIndex(dialog, Control::SystemBoxSystemFields, systemIndexes.value(token, 0));
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
    report.marginLeft = 0;
    report.marginTop = 0;
    report.marginRight = 0;
    report.marginBottom = 0;
    report.paperStyleId = DefaultReportPaperStyleId;
    report.pageWidth = DefaultReportPageWidthMils;
    report.pageHeight = DefaultReportPageHeightMils;
    report.orientation = 0;
    applyDefaultReportFonts(deck, &report);

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

void fitNewReportFramesToForm(ReportDefinition* report)
{
    if (report == nullptr || report->frames.isEmpty()) {
        return;
    }
    const int inset = std::clamp(std::min(report->formWidth, report->formHeight) / 20, 40, 500);
    const int frameHeight = std::clamp(report->formHeight / 18, 120, 360);
    const int frameWidth = std::max(200, report->formWidth - inset * 2);
    int top = inset;
    report->frames[0].bounds = QRect(inset, top, frameWidth, frameHeight);
    if (report->frames.size() > 1) {
        top += frameHeight + std::max(40, frameHeight / 2);
        report->frames[1].bounds = QRect(inset, top, frameWidth, frameHeight);
    }
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

QString virtualUnsavedDeckPath()
{
    constexpr int LegacyUnsavedDeckNameLimit = 100;
    const QDir workingDirectory = QDir::current();
    for (int suffix = 0; suffix < LegacyUnsavedDeckNameLimit; ++suffix) {
        const QString path = workingDirectory.absoluteFilePath(
            QStringLiteral("NONAME%1.~tn").arg(suffix));
        if (!QFileInfo::exists(path)) {
            return QDir::toNativeSeparators(path);
        }
    }
    return QDir::toNativeSeparators(
        workingDirectory.absoluteFilePath(QStringLiteral("NONAME99.~tn")));
}

QString deckDescriptionDisplayPath(const DeckWorkspace* workspace)
{
    if (workspace == nullptr) {
        return virtualUnsavedDeckPath();
    }
    const QString storedPath = workspace->property("cardstackFilePath").toString().trimmed();
    return storedPath.isEmpty()
        ? virtualUnsavedDeckPath()
        : QDir::toNativeSeparators(QFileInfo(storedPath).absoluteFilePath());
}

QString paperSourceDisplayName(int source)
{
    switch (static_cast<QPrinter::PaperSource>(source)) {
    case QPrinter::OnlyOne:
        return QObject::tr("Main tray");
    case QPrinter::Lower:
        return QObject::tr("Lower tray");
    case QPrinter::Middle:
        return QObject::tr("Middle tray");
    case QPrinter::Manual:
        return QObject::tr("Manual feed");
    case QPrinter::Envelope:
        return QObject::tr("Envelope feeder");
    case QPrinter::EnvelopeManual:
        return QObject::tr("Manual envelope feed");
    case QPrinter::Auto:
        return QObject::tr("Automatically Select");
    case QPrinter::Tractor:
        return QObject::tr("Tractor feeder");
    case QPrinter::SmallFormat:
        return QObject::tr("Small format");
    case QPrinter::LargeFormat:
        return QObject::tr("Large format");
    case QPrinter::LargeCapacity:
        return QObject::tr("Large capacity");
    case QPrinter::Cassette:
        return QObject::tr("Cassette");
    case QPrinter::FormSource:
        return QObject::tr("Form source");
    case QPrinter::CustomSource:
    default:
        return QObject::tr("Printer source %1").arg(source);
    }
}

struct PrinterDetails {
    QString driverName;
    QString portName;
    QString comment;
    QStringList sourceNames;
    QVector<int> sourceIds;
    int defaultSource = -1;
    bool hasNativeMetadata = false;
};

#ifdef Q_OS_WIN
PrinterDetails windowsPrinterDetails(const QString& printerName)
{
    PrinterDetails details;
    HANDLE printerHandle = nullptr;
    LPWSTR nativeName = const_cast<LPWSTR>(
        reinterpret_cast<LPCWSTR>(printerName.utf16()));
    if (!OpenPrinterW(nativeName, &printerHandle, nullptr)) {
        return details;
    }

    DWORD requiredBytes = 0;
    GetPrinterW(printerHandle, 2, nullptr, 0, &requiredBytes);
    if (requiredBytes == 0) {
        ClosePrinter(printerHandle);
        return details;
    }

    QByteArray storage(static_cast<int>(requiredBytes), Qt::Uninitialized);
    DWORD returnedBytes = 0;
    if (!GetPrinterW(
            printerHandle,
            2,
            reinterpret_cast<LPBYTE>(storage.data()),
            requiredBytes,
            &returnedBytes)) {
        ClosePrinter(printerHandle);
        return details;
    }

    const auto* info = reinterpret_cast<const PRINTER_INFO_2W*>(storage.constData());
    const auto nativeString = [](LPCWSTR value) {
        return value == nullptr ? QString() : QString::fromWCharArray(value).trimmed();
    };
    details.driverName = nativeString(info->pDriverName);
    details.portName = nativeString(info->pPortName);
    details.comment = nativeString(info->pComment);
    details.hasNativeMetadata = true;
    if (info->pDevMode != nullptr && (info->pDevMode->dmFields & DM_DEFAULTSOURCE) != 0) {
        details.defaultSource = info->pDevMode->dmDefaultSource;
    }

    const int sourceCount = DeviceCapabilitiesW(
        nativeName, info->pPortName, DC_BINS, nullptr, info->pDevMode);
    if (sourceCount > 0) {
        QVector<WORD> sourceIds(sourceCount);
        QVector<wchar_t> sourceNames(sourceCount * 24, L'\0');
        const int idCount = DeviceCapabilitiesW(
            nativeName,
            info->pPortName,
            DC_BINS,
            reinterpret_cast<LPWSTR>(sourceIds.data()),
            info->pDevMode);
        const int nameCount = DeviceCapabilitiesW(
            nativeName,
            info->pPortName,
            DC_BINNAMES,
            sourceNames.data(),
            info->pDevMode);
        const int availableCount = std::min(sourceCount, std::min(idCount, nameCount));
        for (int index = 0; index < availableCount; ++index) {
            const wchar_t* sourceName = sourceNames.constData() + (index * 24);
            int nameLength = 0;
            while (nameLength < 24 && sourceName[nameLength] != L'\0') {
                ++nameLength;
            }
            QString displayName = QString::fromWCharArray(sourceName, nameLength).trimmed();
            if (displayName.isEmpty()) {
                displayName = paperSourceDisplayName(sourceIds.at(index));
            }
            details.sourceNames.append(displayName);
            details.sourceIds.append(sourceIds.at(index));
        }
    }

    ClosePrinter(printerHandle);
    return details;
}
#endif

bool showPrinterSetupDialog(QPrinter* printer, QWidget* parent)
{
    if (printer == nullptr) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Printer Setup"));
    dialog.setProperty("legacyDialogName", QStringLiteral("PRINTERSETUP"));
    dialog.setMinimumWidth(610);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto* printerGroup = new QGroupBox(QObject::tr("Printer"), &dialog);
    auto* printerLayout = new QGridLayout(printerGroup);
    auto* printerCombo = new QComboBox(&dialog);
    printerCombo->setObjectName(QStringLiteral("printerSetupPrinter"));
    auto* propertiesButton = new QPushButton(QObject::tr("&Properties..."), printerGroup);
    propertiesButton->setObjectName(QStringLiteral("printerSetupProperties"));
    auto* statusValue = new QLabel(printerGroup);
    statusValue->setObjectName(QStringLiteral("printerSetupStatus"));
    auto* typeValue = new QLabel(printerGroup);
    typeValue->setObjectName(QStringLiteral("printerSetupType"));
    auto* whereValue = new QLabel(printerGroup);
    whereValue->setObjectName(QStringLiteral("printerSetupWhere"));
    auto* commentValue = new QLabel(printerGroup);
    commentValue->setObjectName(QStringLiteral("printerSetupComment"));

    printerLayout->addWidget(new QLabel(QObject::tr("&Name:"), printerGroup), 0, 0);
    printerLayout->addWidget(printerCombo, 0, 1);
    printerLayout->addWidget(propertiesButton, 0, 2);
    printerLayout->addWidget(new QLabel(QObject::tr("Status:"), printerGroup), 1, 0);
    printerLayout->addWidget(statusValue, 1, 1, 1, 2);
    printerLayout->addWidget(new QLabel(QObject::tr("Type:"), printerGroup), 2, 0);
    printerLayout->addWidget(typeValue, 2, 1, 1, 2);
    printerLayout->addWidget(new QLabel(QObject::tr("Where:"), printerGroup), 3, 0);
    printerLayout->addWidget(whereValue, 3, 1, 1, 2);
    printerLayout->addWidget(new QLabel(QObject::tr("Comment:"), printerGroup), 4, 0);
    printerLayout->addWidget(commentValue, 4, 1, 1, 2);
    printerLayout->setColumnStretch(1, 1);
    layout->addWidget(printerGroup);

    auto* paperCombo = new QComboBox(&dialog);
    paperCombo->setObjectName(QStringLiteral("printerSetupPaperSize"));
    auto* sourceCombo = new QComboBox(&dialog);
    sourceCombo->setObjectName(QStringLiteral("printerSetupPaperSource"));

    auto* paperGroup = new QGroupBox(QObject::tr("Paper"), &dialog);
    auto* paperLayout = new QFormLayout(paperGroup);
    paperLayout->addRow(QObject::tr("&Size:"), paperCombo);
    paperLayout->addRow(QObject::tr("S&ource:"), sourceCombo);

    auto* orientationGroup = new QGroupBox(QObject::tr("Orientation"), &dialog);
    auto* orientationLayout = new QGridLayout(orientationGroup);
    auto* orientationPicture = new QLabel(orientationGroup);
    orientationPicture->setObjectName(QStringLiteral("printerSetupOrientationPicture"));
    orientationPicture->setFixedSize(72, 82);
    orientationPicture->setAlignment(Qt::AlignCenter);
    auto* portrait = new QRadioButton(QObject::tr("&Portrait"), orientationGroup);
    portrait->setObjectName(QStringLiteral("printerSetupPortrait"));
    auto* landscape = new QRadioButton(QObject::tr("&Landscape"), orientationGroup);
    landscape->setObjectName(QStringLiteral("printerSetupLandscape"));
    auto* orientationButtons = new QButtonGroup(&dialog);
    orientationButtons->addButton(portrait);
    orientationButtons->addButton(landscape);
    const bool initiallyLandscape =
        printer->pageLayout().orientation() == QPageLayout::Landscape;
    portrait->setChecked(!initiallyLandscape);
    landscape->setChecked(initiallyLandscape);
    orientationLayout->addWidget(orientationPicture, 0, 0, 2, 1);
    orientationLayout->addWidget(portrait, 0, 1);
    orientationLayout->addWidget(landscape, 1, 1);

    const auto updateOrientationPicture = [orientationPicture, portrait] {
        const bool isLandscape = !portrait->isChecked();
        QPixmap picture(68, 78);
        picture.fill(Qt::transparent);
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF page = isLandscape
            ? QRectF(5, 18, 58, 42)
            : QRectF(14, 5, 40, 66);
        painter.fillRect(page, Qt::white);
        painter.setPen(QPen(Qt::black, 2));
        painter.drawRect(page);
        QFont letterFont(QStringLiteral("Times New Roman"));
        letterFont.setPixelSize(isLandscape ? 30 : 34);
        painter.setFont(letterFont);
        painter.drawText(page, Qt::AlignCenter, QStringLiteral("A"));
        orientationPicture->setPixmap(picture);
    };
    QObject::connect(portrait, &QRadioButton::toggled, &dialog,
                     [updateOrientationPicture](bool) { updateOrientationPicture(); });
    updateOrientationPicture();

    auto* paperOrientationRow = new QHBoxLayout;
    paperOrientationRow->addWidget(paperGroup, 3);
    paperOrientationRow->addWidget(orientationGroup, 2);
    layout->addLayout(paperOrientationRow);

    const QVector<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    int selectedPrinter = -1;
    for (int index = 0; index < printers.size(); ++index) {
        const QPrinterInfo& info = printers.at(index);
        printerCombo->addItem(info.printerName());
        if (info.printerName() == printer->printerName()
            || (selectedPrinter < 0 && printer->printerName().isEmpty() && info.isDefault())) {
            selectedPrinter = index;
        }
    }
    if (printers.isEmpty()) {
        printerCombo->addItem(QObject::tr("System default"));
        printerCombo->setEnabled(false);
    } else {
        printerCombo->setCurrentIndex(std::max(0, selectedPrinter));
    }

    const auto printerStateText = [](QPrinter::PrinterState state) {
        switch (state) {
        case QPrinter::Idle:
            return QObject::tr("Ready");
        case QPrinter::Active:
            return QObject::tr("Active");
        case QPrinter::Aborted:
            return QObject::tr("Aborted");
        case QPrinter::Error:
        default:
            return QObject::tr("Error");
        }
    };
    QVector<int> paperSourceIds;
    const auto refreshPrinterDetails = [&] {
        const int index = printerCombo->currentIndex();
        paperSourceIds.clear();
        sourceCombo->clear();
        if (index < 0 || index >= printers.size()) {
            statusValue->setText(QObject::tr("Unavailable"));
            typeValue->clear();
            whereValue->clear();
            commentValue->clear();
            propertiesButton->setEnabled(false);
            sourceCombo->addItem(QObject::tr("Printer default"));
            sourceCombo->setEnabled(false);
            return;
        }
        const QPrinterInfo& info = printers.at(index);
        QPrinter selectedPrinterInfo(QPrinter::HighResolution);
        selectedPrinterInfo.setPrinterName(info.printerName());
        statusValue->setText(printerStateText(selectedPrinterInfo.printerState()));
        QString type = info.makeAndModel().trimmed();
        if (type.isEmpty()) {
            type = info.printerName();
        }
        QString location = info.location().trimmed();
        QString comment = info.description().trimmed();
        if (comment.compare(info.printerName(), Qt::CaseInsensitive) == 0
            || comment.compare(type, Qt::CaseInsensitive) == 0) {
            comment.clear();
        }
#ifdef Q_OS_WIN
        const PrinterDetails nativeDetails = windowsPrinterDetails(info.printerName());
        if (nativeDetails.hasNativeMetadata) {
            if (!nativeDetails.driverName.isEmpty()) {
                type = nativeDetails.driverName;
            }
            if (!nativeDetails.portName.isEmpty()) {
                location = nativeDetails.portName;
            }
            comment = nativeDetails.comment;
            paperSourceIds = nativeDetails.sourceIds;
            sourceCombo->addItems(nativeDetails.sourceNames);
            int selectedSource = nativeDetails.defaultSource;
            if (info.printerName() == printer->printerName()) {
                selectedSource = static_cast<int>(printer->paperSource());
            }
            const int selectedSourceIndex = paperSourceIds.indexOf(selectedSource);
            sourceCombo->setCurrentIndex(selectedSourceIndex >= 0 ? selectedSourceIndex : 0);
        }
#endif
        if (paperSourceIds.isEmpty()) {
#ifdef Q_OS_WIN
            const QList<QPrinter::PaperSource> supportedSources =
                selectedPrinterInfo.supportedPaperSources();
            for (QPrinter::PaperSource source : supportedSources) {
                paperSourceIds.append(static_cast<int>(source));
                sourceCombo->addItem(paperSourceDisplayName(static_cast<int>(source)));
            }
            const int selectedSourceIndex = paperSourceIds.indexOf(
                static_cast<int>(selectedPrinterInfo.paperSource()));
            sourceCombo->setCurrentIndex(selectedSourceIndex >= 0 ? selectedSourceIndex : 0);
#endif
        }
        if (paperSourceIds.isEmpty()) {
            sourceCombo->addItem(QObject::tr("Printer default"));
        }
        sourceCombo->setEnabled(paperSourceIds.size() > 1);
        typeValue->setText(type);
        whereValue->setText(location);
        commentValue->setText(comment);
        propertiesButton->setEnabled(true);
    };

    QVector<QPageSize> paperSizes;
    auto refreshPaperSizes = [&]() {
        const QPageSize currentPageSize = printer->pageLayout().pageSize();
        paperSizes.clear();
        paperCombo->clear();
        const int printerIndex = printerCombo->currentIndex();
        if (printerIndex >= 0 && printerIndex < printers.size()) {
            paperSizes = printers.at(printerIndex).supportedPageSizes();
        }
        if (paperSizes.isEmpty()) {
            paperSizes.append(currentPageSize);
        }
        int currentPaperIndex = 0;
        for (int index = 0; index < paperSizes.size(); ++index) {
            const QString name = paperSizes.at(index).name().trimmed().isEmpty()
                ? QObject::tr("Current paper size")
                : paperSizes.at(index).name();
            paperCombo->addItem(name);
            if (paperSizes.at(index).id() == currentPageSize.id()) {
                currentPaperIndex = index;
            }
        }
        paperCombo->setCurrentIndex(currentPaperIndex);
    };
    QObject::connect(printerCombo, &QComboBox::currentIndexChanged, &dialog,
                     [&refreshPaperSizes, &refreshPrinterDetails](int) {
                         refreshPaperSizes();
                         refreshPrinterDetails();
                     });
    refreshPaperSizes();
    refreshPrinterDetails();

    QObject::connect(propertiesButton, &QPushButton::clicked, &dialog, [&] {
        const int index = printerCombo->currentIndex();
        if (index < 0 || index >= printers.size()) {
            return;
        }
#ifdef Q_OS_WIN
        const QString printerName = printers.at(index).printerName();
        HANDLE printerHandle = nullptr;
        LPWSTR nativeName = const_cast<LPWSTR>(
            reinterpret_cast<LPCWSTR>(printerName.utf16()));
        if (!OpenPrinterW(nativeName, &printerHandle, nullptr)) {
            return;
        }
        const LONG devModeSize = DocumentPropertiesW(
            reinterpret_cast<HWND>(dialog.winId()), printerHandle, nativeName,
            nullptr, nullptr, 0);
        if (devModeSize > 0) {
            QByteArray storage(devModeSize, Qt::Uninitialized);
            auto* devMode = reinterpret_cast<DEVMODEW*>(storage.data());
            if (DocumentPropertiesW(
                    reinterpret_cast<HWND>(dialog.winId()), printerHandle, nativeName,
                    devMode, nullptr, DM_OUT_BUFFER) == IDOK) {
                DocumentPropertiesW(
                    reinterpret_cast<HWND>(dialog.winId()), printerHandle, nativeName,
                    devMode, devMode, DM_IN_BUFFER | DM_OUT_BUFFER | DM_IN_PROMPT);
            }
        }
        ClosePrinter(printerHandle);
#else
        QMessageBox::information(
            &dialog,
            QObject::tr("Printer Properties"),
            QObject::tr("Printer driver properties are managed by the operating system."));
#endif
        refreshPrinterDetails();
        refreshPaperSizes();
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help,
        Qt::Horizontal, &dialog);
    auto* networkButton = new QPushButton(QObject::tr("&Network..."), &dialog);
    networkButton->setObjectName(QStringLiteral("printerSetupNetwork"));
    buttons->addButton(networkButton, QDialogButtonBox::ActionRole);
#ifdef Q_OS_WIN
    QObject::connect(networkButton, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:printers")));
    });
#else
    networkButton->setVisible(false);
#endif
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::Help), &QAbstractButton::clicked, &dialog, [&dialog]() {
        QMessageBox::information(
            &dialog,
            QObject::tr("Printer Setup"),
            QObject::tr("Printer setup changes the output device, paper, and orientation. Report forms control page margins."));
    });
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const int printerIndex = printerCombo->currentIndex();
    if (printerIndex >= 0 && printerIndex < printers.size()) {
        printer->setPrinterName(printers.at(printerIndex).printerName());
    }
    if (paperCombo->currentIndex() >= 0 && paperCombo->currentIndex() < paperSizes.size()) {
        printer->setPageSize(paperSizes.at(paperCombo->currentIndex()));
    }
    if (sourceCombo->currentIndex() >= 0 && sourceCombo->currentIndex() < paperSourceIds.size()) {
        printer->setPaperSource(
            static_cast<QPrinter::PaperSource>(paperSourceIds.at(sourceCombo->currentIndex())));
    }
    printer->setPageOrientation(
        landscape->isChecked() ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer->setFullPage(true);
    return true;
}

} // namespace

QFont MainWindow::fontForDeckRole(const QString& serialized)
{
    return storedDeckFont(serialized);
}

MainWindow::MainWindow(QWidget* parent, bool openInitialSample, bool restorePreviousSession)
    : QMainWindow(parent)
    , m_mdiArea(new QMdiArea(this))
    , m_printer(std::make_shared<QPrinter>(QPrinter::HighResolution))
{
    initializeCardStackApplicationResources();
    setWindowTitle(QStringLiteral("CardStack"));
    loadPhoneDialerSettings();
    if (windowIcon().isNull()) {
        setWindowIcon(cardStackIcon());
    }
    m_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(m_mdiArea);
    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, [this]() {
        refreshMenuForActiveWindow();
        updateCommandState();
    });

    createMenus();
    createToolBar();
    createIndexBar();
    connect(m_mdiArea->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        resizeIndexBarButtons();
    });
    connect(m_mdiArea->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        resizeIndexBarButtons();
    });
    qApp->installEventFilter(this);
    const bool restoredDeckSession = restorePreviousSession && restoreWindowSession();
    if (openInitialSample && !restoredDeckSession) {
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
    const QByteArray deckWindows = captureDeckWindowSession();
    if (!closeAllSubWindowsWithPrompts()) {
        event->ignore();
        return;
    }

    saveWindowSession(deckWindows);
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (m_pendingDeckWindowSession.isEmpty()) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(m_pendingDeckWindowSession);
    m_pendingDeckWindowSession.clear();
    if (!document.isArray()) {
        return;
    }

    QMdiSubWindow* activeWindow = nullptr;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString filePath = object.value(QStringLiteral("path")).toString();
        if (filePath.isEmpty() || !QFileInfo::exists(filePath) || !openDeckFromPath(filePath)) {
            continue;
        }

        QMdiSubWindow* subWindow = m_mdiArea->activeSubWindow();
        if (subWindow == nullptr || qobject_cast<DeckWorkspace*>(subWindow->widget()) == nullptr) {
            continue;
        }
        const QRect geometry(
            object.value(QStringLiteral("x")).toInt(),
            object.value(QStringLiteral("y")).toInt(),
            std::max(1, object.value(QStringLiteral("width")).toInt()),
            std::max(1, object.value(QStringLiteral("height")).toInt()));
        subWindow->showNormal();
        subWindow->setGeometry(geometry);
        if (object.value(QStringLiteral("minimized")).toBool()) {
            subWindow->showMinimized();
        } else if (object.value(QStringLiteral("maximized")).toBool()) {
            subWindow->showMaximized();
        }
        if (object.value(QStringLiteral("active")).toBool()) {
            activeWindow = subWindow;
        }
    }
    if (activeWindow != nullptr) {
        m_mdiArea->setActiveSubWindow(activeWindow);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize
        && (watched == m_indexBar || watched->objectName() == QStringLiteral("indexBarContainer"))) {
        resizeIndexBarButtons();
    }

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
        if ((keyEvent->key() == Qt::Key_PageUp || keyEvent->key() == Qt::Key_PageDown)
            && (keyEvent->modifiers() == Qt::NoModifier
                || keyEvent->modifiers() == Qt::ControlModifier)) {
            DeckWorkspace* workspace = activeDeckWorkspace();
            if (workspace != nullptr) {
                const bool tableMode = workspace->viewMode() == DeckWorkspace::ViewMode::Table;
                if (tableMode && keyEvent->modifiers() == Qt::ControlModifier) {
                    return QMainWindow::eventFilter(watched, event);
                }

                const bool windowful = tableMode || keyEvent->modifiers() == Qt::ControlModifier;
                const int commandId = keyEvent->key() == Qt::Key_PageUp
                    ? (windowful ? Command::NavigatePreviousWindowful : Command::NavigatePreviousCard)
                    : (windowful ? Command::NavigateNextWindowful : Command::NavigateNextCard);
                if (QAction* action = findUiAction(commandId); action != nullptr && action->isEnabled()) {
                    action->trigger();
                    return true;
                }
            }
        }
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
    if (menuId == Menu::MainDeck && findUiAction(Command::EditSmartPaste) == nullptr) {
        for (QMenu* menu : menuBar()->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly)) {
            QString title = menu->title();
            title.remove(QLatin1Char('&'));
            if (title.compare(tr("Edit"), Qt::CaseInsensitive) == 0) {
                QAction* smartPaste = menu->addAction(tr("Smart Paste"));
                smartPaste->setData(Command::EditSmartPaste);
                smartPaste->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
                connect(smartPaste, &QAction::triggered, this, &MainWindow::handleUiAction);
                break;
            }
        }
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

    const bool buttonBarsVisible = m_buttonBar == nullptr || !m_buttonBar->isHidden();
    m_indexBar->setVisible(buttonBarsVisible && activeDeckWorkspace() != nullptr);
    resizeIndexBarButtons();
}

void MainWindow::updateToolbarCardPosition()
{
    if (m_deckModeLabel != nullptr) {
        DeckWorkspace* workspace = activeDeckWorkspace();
        if (workspace == nullptr) {
            m_deckModeLabel->clear();
            m_deckModeLabel->setVisible(false);
        } else {
            m_deckModeLabel->setVisible(true);
            m_deckModeLabel->setText(workspace->viewMode() == DeckWorkspace::ViewMode::Table
                                         ? tr("Table View")
                                         : tr("Card View"));
        }
    }

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
    QAction* next = menu->addAction(tr("&Next"));
    next->setProperty("cardstackWindowCycleAction", true);
    next->setShortcut(QKeySequence(QStringLiteral("Ctrl+F6")));
    connect(next, &QAction::triggered, m_mdiArea, &QMdiArea::activateNextSubWindow);

    QAction* previous = menu->addAction(tr("&Previous"));
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

void MainWindow::arrangeMinimizedSubWindows()
{
    QList<QMdiSubWindow*> minimizedWindows;
    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        if (subWindow != nullptr && subWindow->isVisible() && subWindow->isMinimized()) {
            minimizedWindows.append(subWindow);
        }
    }
    if (minimizedWindows.isEmpty()) {
        return;
    }

    constexpr int IconMarginPx = 4;
    constexpr int IconSpacingPx = 4;
    constexpr int MinimumIconWidthPx = 160;
    constexpr int MaximumIconWidthPx = 240;
    constexpr int MinimumIconHeightPx = 28;
    constexpr int MaximumIconHeightPx = 48;
    const QRect area = m_mdiArea->viewport()->rect().adjusted(
        IconMarginPx, IconMarginPx, -IconMarginPx, -IconMarginPx);
    const int maximumWidth = std::max(1, std::min(MaximumIconWidthPx, area.width()));
    int left = area.left();
    int rowBottom = area.bottom();
    int rowHeight = 0;

    for (QMdiSubWindow* subWindow : minimizedWindows) {
        const int width = std::clamp(subWindow->width(), std::min(MinimumIconWidthPx, maximumWidth), maximumWidth);
        const int height = std::clamp(subWindow->height(), MinimumIconHeightPx, MaximumIconHeightPx);
        if (left > area.left() && left + width - 1 > area.right()) {
            left = area.left();
            rowBottom -= rowHeight + IconSpacingPx;
            rowHeight = 0;
        }
        subWindow->setGeometry(left, rowBottom - height + 1, width, height);
        left += width + IconSpacingPx;
        rowHeight = std::max(rowHeight, height);
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

    m_indexBar->setMinimumWidth(0);
    m_indexBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* container = new IndexBarContainer(m_indexBar);
    container->setObjectName(QStringLiteral("indexBarContainer"));
    container->setMinimumWidth(0);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

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
        auto* button = new IndexBarButton(definition.label, container);
        button->setObjectName(QStringLiteral("index_%1").arg(definition.key));
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(false);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
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
    const int availableWidth = std::max(1, container->contentsRect().width());
    const int marginWidth = IndexBarHorizontalMarginPx * 2;
    const int gapWidth = IndexBarButtonGapPx * std::max(0, buttonCount - 1);
    int widestIndexGlyphWidth = 1;
    const QFontMetrics indexMetrics = buttons.first()->fontMetrics();
    for (const QPushButton* button : buttons) {
        if (!button->text().isEmpty()) {
            widestIndexGlyphWidth = std::max(widestIndexGlyphWidth, indexMetrics.horizontalAdvance(button->text()));
        }
    }
    const int minimumButtonWidth = widestIndexGlyphWidth + IndexBarNativeTextPaddingPx;
    const int buttonWidth = std::max(
        minimumButtonWidth,
        (availableWidth - marginWidth - gapWidth) / buttonCount);
    const int usedWidth = buttonWidth * buttonCount + gapWidth + marginWidth;
    const int overflowWidth = std::max(0, usedWidth - availableWidth);
    int scrollOffset = 0;
    if (overflowWidth > 0 && m_mdiArea != nullptr) {
        const QScrollBar* horizontalScrollBar = m_mdiArea->horizontalScrollBar();
        const int scrollRange = horizontalScrollBar->maximum() - horizontalScrollBar->minimum();
        if (scrollRange > 0) {
            scrollOffset = overflowWidth
                * (horizontalScrollBar->value() - horizontalScrollBar->minimum())
                / scrollRange;
        }
    }
    const int buttonHeight = std::clamp(
        m_indexBar->contentsRect().height() - IndexBarVerticalMarginPx * 2,
        IndexBarButtonMinHeightPx,
        IndexBarButtonMaxHeightPx);

    container->setProperty("indexContentWidth", usedWidth);
    for (int index = 0; index < buttonCount; ++index) {
        QPushButton* button = buttons.at(index);
        button->setGeometry(
            IndexBarHorizontalMarginPx + index * (buttonWidth + IndexBarButtonGapPx) - scrollOffset,
            IndexBarVerticalMarginPx,
            buttonWidth,
            buttonHeight);
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

    if (m_designerPropertyToolbar != nullptr) {
        m_designerPropertyToolbar->deleteLater();
        m_designerPropertyToolbar = nullptr;
    }
    m_buttonBar->clear();
    m_cardPositionLabel = nullptr;
    m_deckModeLabel = nullptr;
    m_buttonBar->setProperty("cardstackToolbarMenuId", menuId);

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

    const auto addUiToolAction = [this, findPersistentAction](int commandId, const QString& iconName, const QString& toolTip) {
        QAction* action = findPersistentAction(commandId);
        if (action == nullptr) {
            action = new QAction(toolTip, this);
            action->setData(commandId);
            action->setEnabled(false);
            connect(action, &QAction::triggered, this, &MainWindow::handleUiAction);
        }
        action->setIcon(cardStackToolbarIcon(iconName));
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
        m_deckModeLabel = new QLabel(m_buttonBar);
        m_deckModeLabel->setObjectName(QStringLiteral("toolbarDeckModeLabel"));
        m_deckModeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_deckModeLabel->setMinimumWidth(88);
        m_deckModeLabel->setContentsMargins(8, 0, 8, 0);
        m_buttonBar->addWidget(m_deckModeLabel);
        updateToolbarCardPosition();
    };

    addUiToolAction(Command::FileNew, QStringLiteral("deck-new"), tr("New deck"));
    addUiToolAction(Command::FileOpen, QStringLiteral("deck-open"), tr("Open deck"));
    addSeparator();
    addUiToolAction(Command::PhoneDial, QStringLiteral("phone-dial"), tr("Phone Dialer"));
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
        addSeparator();
        m_designerPropertyToolbar = new QWidget(m_buttonBar);
        m_designerPropertyToolbar->setObjectName(QStringLiteral("designerPropertyToolbar"));
        m_buttonBar->addWidget(m_designerPropertyToolbar);
        rebuildDesignerPropertyToolbar();
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
        addSeparator();
        m_designerPropertyToolbar = new QWidget(m_buttonBar);
        m_designerPropertyToolbar->setObjectName(QStringLiteral("designerPropertyToolbar"));
        m_buttonBar->addWidget(m_designerPropertyToolbar);
        rebuildDesignerPropertyToolbar();
        return;
    }

    addUiToolAction(Command::FilePrintReport, QStringLiteral("print-report"), tr("Print report"));
    addSeparator();
    addUiToolAction(Command::SearchFind, QStringLiteral("find"), tr("Find"));
    addUiToolAction(Command::SearchFindNext, QStringLiteral("find-next"), tr("Find next"));
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

void MainWindow::rebuildDesignerPropertyToolbar()
{
    if (m_designerPropertyToolbar == nullptr) {
        return;
    }
    if (QLayout* oldLayout = m_designerPropertyToolbar->layout()) {
        delete oldLayout;
    }
    qDeleteAll(m_designerPropertyToolbar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));

    auto* layout = new QHBoxLayout(m_designerPropertyToolbar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    const auto addLabel = [this, layout](const QString& text) {
        auto* label = new QLabel(text, m_designerPropertyToolbar);
        layout->addWidget(label);
        return label;
    };
    const auto addToggle = [this, layout](const QString& text, bool checked) {
        auto* button = new QToolButton(m_designerPropertyToolbar);
        button->setText(text);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setAutoRaise(false);
        layout->addWidget(button);
        return button;
    };
    const auto addIconToggle = [this, layout](
                                   const QString& iconName,
                                   const QString& accessibleText,
                                   bool checked) {
        auto* button = new QToolButton(m_designerPropertyToolbar);
        button->setIcon(cardStackToolbarIcon(iconName));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(accessibleText);
        button->setAccessibleName(accessibleText);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setAutoRaise(false);
        button->setFixedWidth(28);
        layout->addWidget(button);
        return button;
    };

    if (TemplateDesignerWidget* designer = activeTemplateDesigner()) {
        const CardTemplateFrame* frame = designer->selectedFrameDefinition();
        if (frame == nullptr) {
            addLabel(tr("Deck width (in): %1").arg(designer->layoutDefinition().canvasWidth / 1000.0, 0, 'f', 2));
            addLabel(tr("Deck height (in): %1").arg(designer->layoutDefinition().canvasHeight / 1000.0, 0, 'f', 2));
            layout->addStretch(1);
            return;
        }

        if (frame->kind == CardTemplateFrameKind::DataBox || frame->kind == CardTemplateFrameKind::NotesBox) {
            const int fieldIndex = designer->selectedFieldIndex();
            if (fieldIndex < 0 || fieldIndex >= designer->fieldDefinitions().size()) {
                addLabel(tr("No field selected"));
                return;
            }
            const FieldDefinition field = designer->fieldDefinitions().at(fieldIndex);
            addLabel(tr("Name:"));
            auto* nameEdit = new QLineEdit(field.name(), m_designerPropertyToolbar);
            nameEdit->setObjectName(QStringLiteral("designerFieldNameEdit"));
            nameEdit->setMinimumWidth(150);
            layout->addWidget(nameEdit);
            addLabel(tr("Length:"));
            auto* lengthSpin = new QSpinBox(m_designerPropertyToolbar);
            lengthSpin->setObjectName(QStringLiteral("designerFieldLengthSpin"));
            lengthSpin->setRange(1, 32767);
            lengthSpin->setValue(field.maxLength());
            layout->addWidget(lengthSpin);
            auto* phone = new QCheckBox(tr("Phone"), m_designerPropertyToolbar);
            phone->setObjectName(QStringLiteral("designerFieldPhoneCheck"));
            phone->setChecked(field.isPhone());
            layout->addWidget(phone);
            auto* showName = new QCheckBox(tr("Show name"), m_designerPropertyToolbar);
            showName->setObjectName(QStringLiteral("designerFieldShowNameCheck"));
            showName->setChecked(field.showName());
            layout->addWidget(showName);
            const auto apply = [designer, nameEdit, lengthSpin, phone, showName]() {
                designer->updateSelectedFieldDefinition(
                    nameEdit->text(), lengthSpin->value(), phone->isChecked(), showName->isChecked());
            };
            connect(nameEdit, &QLineEdit::editingFinished, designer, apply);
            connect(lengthSpin, &QSpinBox::valueChanged, designer, [apply](int) { apply(); });
            connect(phone, &QCheckBox::toggled, designer, [apply](bool) { apply(); });
            connect(showName, &QCheckBox::toggled, designer, [apply](bool) { apply(); });
        } else if (frame->kind == CardTemplateFrameKind::Text) {
            addLabel(tr("Text:"));
            auto* textEdit = new QLineEdit(frame->text, m_designerPropertyToolbar);
            textEdit->setObjectName(QStringLiteral("designerTemplateTextEdit"));
            textEdit->setMinimumWidth(220);
            layout->addWidget(textEdit);
            auto* left = addIconToggle(QStringLiteral("align-left"), tr("Align left"), (frame->styleFlags & (CardTemplateStyleFlagAlignCenter | CardTemplateStyleFlagAlignRight)) == 0);
            auto* center = addIconToggle(QStringLiteral("align-center"), tr("Align center"), (frame->styleFlags & CardTemplateStyleFlagAlignCenter) != 0);
            auto* right = addIconToggle(QStringLiteral("align-right"), tr("Align right"), (frame->styleFlags & CardTemplateStyleFlagAlignRight) != 0);
            const auto apply = [designer, textEdit, left, center, right, style = frame->styleFlags]() {
                quint8 flags = style & (CardTemplateStyleFlagBold | CardTemplateStyleFlagItalic | CardTemplateStyleFlagUnderline);
                flags |= right->isChecked() ? CardTemplateStyleFlagAlignRight
                    : (center->isChecked() ? CardTemplateStyleFlagAlignCenter : 0);
                designer->updateSelectedFrameFromToolbar(textEdit->text(), flags, CardTemplateLineBoxShape::Box, 0, 0, 0);
                left->setChecked(!center->isChecked() && !right->isChecked());
            };
            connect(textEdit, &QLineEdit::editingFinished, designer, apply);
            for (QToolButton* button : {left, center, right}) {
                connect(button, &QToolButton::clicked, designer, [left, center, right, button, apply]() {
                    left->setChecked(button == left);
                    center->setChecked(button == center);
                    right->setChecked(button == right);
                    apply();
                });
            }
        } else {
            addLabel(tr("Shape:"));
            auto* shape = new QComboBox(m_designerPropertyToolbar);
            shape->setObjectName(QStringLiteral("designerTemplateShapeCombo"));
            shape->addItem(tr("Box"), static_cast<int>(CardTemplateLineBoxShape::Box));
            shape->addItem(tr("Horizontal"), static_cast<int>(CardTemplateLineBoxShape::HorizontalLine));
            shape->addItem(tr("Vertical"), static_cast<int>(CardTemplateLineBoxShape::VerticalLine));
            shape->setCurrentIndex(std::max(0, shape->findData(static_cast<int>(frame->lineBoxShape))));
            layout->addWidget(shape);
            connect(shape, &QComboBox::currentIndexChanged, designer, [designer, shape, frame](int) {
                designer->updateSelectedFrameFromToolbar(
                    {}, frame->styleFlags, static_cast<CardTemplateLineBoxShape>(shape->currentData().toInt()),
                    frame->lineStyle, frame->fillPattern, frame->cornerRadius);
            });
        }
        layout->addStretch(1);
        return;
    }

    ReportDesignerWidget* designer = activeReportDesigner();
    if (designer == nullptr) {
        return;
    }
    const ReportFrameDefinition* frame = designer->selectedFrameDefinition();
    if (frame == nullptr) {
        addLabel(tr("Report width (in): %1").arg(designer->report().formWidth / 1000.0, 0, 'f', 2));
        addLabel(tr("Report height (in): %1").arg(designer->report().formHeight / 1000.0, 0, 'f', 2));
        layout->addStretch(1);
        return;
    }

    if (frame->kind == ReportFrameKind::LineOrBox) {
        addLabel(tr("Shape:"));
        auto* shape = new QComboBox(m_designerPropertyToolbar);
        shape->setObjectName(QStringLiteral("designerReportShapeCombo"));
        shape->addItem(tr("Box"), ReportLineShapeBox);
        shape->addItem(tr("Horizontal"), ReportLineShapeHorizontal);
        shape->addItem(tr("Vertical"), ReportLineShapeVertical);
        shape->setCurrentIndex(std::max(0, shape->findData(frame->lineBoxShape)));
        layout->addWidget(shape);
        addLabel(tr("Line style:"));
        auto* lineStyle = new QComboBox(m_designerPropertyToolbar);
        lineStyle->setObjectName(QStringLiteral("designerReportLineStyleCombo"));
        lineStyle->addItems(ReportStyleCatalog::lineStyleNames());
        lineStyle->setCurrentIndex(std::clamp(frame->lineStyle, 0, lineStyle->count() - 1));
        layout->addWidget(lineStyle);
        addLabel(tr("Fill pattern:"));
        auto* fill = new QComboBox(m_designerPropertyToolbar);
        fill->setObjectName(QStringLiteral("designerReportFillPatternCombo"));
        fill->addItems(ReportStyleCatalog::fillPatternNames());
        fill->setCurrentIndex(std::clamp(frame->fillPattern, 0, fill->count() - 1));
        layout->addWidget(fill);
        addLabel(tr("Corner radius:"));
        auto* radius = new QSpinBox(m_designerPropertyToolbar);
        radius->setObjectName(QStringLiteral("designerReportCornerRadiusSpin"));
        radius->setRange(0, 10000);
        radius->setValue(frame->cornerRadius);
        layout->addWidget(radius);
        const auto apply = [designer, shape, lineStyle, fill, radius]() {
            designer->updateSelectedFrameFromToolbar(
                {}, 0, false, shape->currentData().toInt(), lineStyle->currentIndex(), fill->currentIndex(), radius->value());
        };
        connect(shape, &QComboBox::currentIndexChanged, designer, [apply](int) { apply(); });
        connect(lineStyle, &QComboBox::currentIndexChanged, designer, [apply](int) { apply(); });
        connect(fill, &QComboBox::currentIndexChanged, designer, [apply](int) { apply(); });
        connect(radius, &QSpinBox::valueChanged, designer, [apply](int) { apply(); });
    } else {
        addLabel(frame->kind == ReportFrameKind::Data ? tr("Field:") : tr("Text:"));
        QLineEdit* textEdit = nullptr;
        QComboBox* fieldCombo = nullptr;
        if (frame->kind == ReportFrameKind::Data) {
            fieldCombo = new QComboBox(m_designerPropertyToolbar);
            fieldCombo->setObjectName(QStringLiteral("designerReportFieldCombo"));
            fieldCombo->addItems(designer->fieldNames());
            const QString selectedField = frame->fieldPlaceholders.isEmpty() ? QString() : frame->fieldPlaceholders.first();
            fieldCombo->setCurrentIndex(std::max(0, fieldCombo->findText(selectedField)));
            layout->addWidget(fieldCombo);
        } else {
            textEdit = new QLineEdit(frame->text, m_designerPropertyToolbar);
            textEdit->setObjectName(QStringLiteral("designerReportTextEdit"));
            textEdit->setMinimumWidth(200);
            layout->addWidget(textEdit);
        }
        auto* bold = addToggle(tr("B"), (frame->styleFlags & ReportStyleFlagBold) != 0);
        auto* italic = addToggle(tr("I"), (frame->styleFlags & ReportStyleFlagItalic) != 0);
        auto* underline = addToggle(tr("U"), (frame->styleFlags & ReportStyleFlagUnderline) != 0);
        auto* left = addIconToggle(QStringLiteral("align-left"), tr("Align left"), (frame->styleFlags & (ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight)) == 0);
        auto* center = addIconToggle(QStringLiteral("align-center"), tr("Align center"), (frame->styleFlags & ReportStyleFlagAlignCenter) != 0);
        auto* right = addIconToggle(QStringLiteral("align-right"), tr("Align right"), (frame->styleFlags & ReportStyleFlagAlignRight) != 0);
        auto* printEntire = new QCheckBox(tr("Print Entire"), m_designerPropertyToolbar);
        printEntire->setObjectName(QStringLiteral("designerReportPrintEntireCheck"));
        printEntire->setChecked(frame->printEntireContentsFlag != 0);
        printEntire->setVisible(frame->kind == ReportFrameKind::Data);
        layout->addWidget(printEntire);
        const auto apply = [designer, frameKind = frame->kind, textEdit, fieldCombo, bold, italic, underline, left, center, right, printEntire]() {
            const QString text = frameKind == ReportFrameKind::Data
                ? QStringLiteral("[%1]").arg(fieldCombo->currentText())
                : textEdit->text();
            quint8 flags = bold->isChecked() ? ReportStyleFlagBold : 0;
            flags |= italic->isChecked() ? ReportStyleFlagItalic : 0;
            flags |= underline->isChecked() ? ReportStyleFlagUnderline : 0;
            flags |= right->isChecked() ? ReportStyleFlagAlignRight
                : (center->isChecked() ? ReportStyleFlagAlignCenter : 0);
            designer->updateSelectedFrameFromToolbar(text, flags, printEntire->isChecked(), 0, 0, 0, 0);
            left->setChecked(!center->isChecked() && !right->isChecked());
        };
        if (textEdit != nullptr) {
            connect(textEdit, &QLineEdit::editingFinished, designer, apply);
        }
        if (fieldCombo != nullptr) {
            connect(fieldCombo, &QComboBox::currentIndexChanged, designer, [apply](int) { apply(); });
        }
        for (QToolButton* button : {bold, italic, underline}) {
            connect(button, &QToolButton::clicked, designer, [apply]() { apply(); });
        }
        for (QToolButton* button : {left, center, right}) {
            connect(button, &QToolButton::clicked, designer, [left, center, right, button, apply]() {
                left->setChecked(button == left);
                center->setChecked(button == center);
                right->setChecked(button == right);
                apply();
            });
        }
        connect(printEntire, &QCheckBox::toggled, designer, [apply](bool) { apply(); });
    }
    layout->addStretch(1);
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
        m_printer->setFullPage(true);
        showPrinterSetupDialog(m_printer.get(), this);
        return;
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
        arrangeMinimizedSubWindows();
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
    case Command::PhoneCallLog:
        handlePhoneCallLogCommand();
        return;
    case Command::ConfigureAddSecurity:
        handleSecurityCommand();
        return;
    case Command::ConfigureDeckDescription:
        handleDeckDescriptionCommand();
        return;
    case Command::ConfigureShowButtonBar:
        if (m_buttonBar != nullptr) {
            const bool showButtonBars = m_buttonBar->isHidden();
            m_buttonBar->setVisible(showButtonBars);
            if (m_indexBar != nullptr) {
                m_indexBar->setVisible(showButtonBars && activeDeckWorkspace() != nullptr);
            }
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
        const QFont font = QFontDialog::getFont(
            &accepted, fontForDeckRole(workspace->deck().appearance().dataFont), this, tr("Data Font"));
        if (accepted) {
            workspace->applyDataFont(font);
        }
        return;
    }
    case Command::ConfigureNameFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(
            &accepted, fontForDeckRole(workspace->deck().appearance().nameFont), this, tr("Name Font"));
        if (accepted) {
            workspace->applyNameFont(font);
        }
        return;
    }
    case Command::ConfigureTextFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(
            &accepted, fontForDeckRole(workspace->deck().appearance().textFont), this, tr("Text Font"));
        if (accepted) {
            workspace->applyTextFont(font);
        }
        return;
    }
    case Command::ConfigureIndexFont: {
        bool accepted = false;
        const QFont font = QFontDialog::getFont(
            &accepted, fontForDeckRole(workspace->deck().appearance().indexFont), this, tr("Index Font"));
        if (accepted) {
            workspace->applyIndexFont(font);
        }
        return;
    }
    case Command::ConfigureColors: {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"), this, dialogContext());
        if (!dialog) {
            return;
        }
        const DeckAppearance current = workspace->deck().appearance();
        QStringList colors;
        for (const QString& color : current.customColors) {
            colors.append(color);
        }
        UiBuilder::setColorDialogState(dialog.get(), colors, current.useSystemColors);
        if (dialog->exec() != QDialog::Accepted) {
            return;
        }
        DeckAppearance updated = current;
        const QStringList selectedColors = UiBuilder::colorDialogColors(dialog.get());
        updated.customColors.clear();
        for (const QString& color : selectedColors) {
            updated.customColors.append(color);
        }
        updated.useSystemColors = UiBuilder::colorDialogUsesSystemColors(dialog.get());
        workspace->applyAppearance(std::move(updated));
        return;
    }
    case Command::ConfigureIndex:
        handleSortCommand();
        return;
    case Command::NavigateFirstCard:
        workspace->firstCard();
        return;
    case Command::NavigateLastCard:
        workspace->lastCard();
        return;
    case Command::NavigatePreviousWindowful: {
        int pageSize = 1;
        if (workspace->viewMode() == DeckWorkspace::ViewMode::Table) {
            if (QTableView* table = workspace->findChild<QTableView*>()) {
                if (table->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
                    pageSize = table->verticalScrollBar()->pageStep();
                } else {
                    const int firstRow = table->rowAt(0);
                    const int lastRow = table->rowAt(std::max(0, table->viewport()->height() - 1));
                    if (firstRow >= 0 && lastRow >= firstRow) {
                        pageSize = lastRow - firstRow + 1;
                    }
                }
            }
        }
        pageSize = std::clamp(pageSize, 1, 31);
        for (int step = 0; step < pageSize; ++step) {
            workspace->previousCard();
        }
        return;
    }
    case Command::NavigatePreviousCard:
        workspace->previousCard();
        return;
    case Command::NavigateNextCard:
        workspace->nextCard();
        return;
    case Command::NavigateNextWindowful: {
        int pageSize = 1;
        if (workspace->viewMode() == DeckWorkspace::ViewMode::Table) {
            if (QTableView* table = workspace->findChild<QTableView*>()) {
                if (table->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
                    pageSize = table->verticalScrollBar()->pageStep();
                } else {
                    const int firstRow = table->rowAt(0);
                    const int lastRow = table->rowAt(std::max(0, table->viewport()->height() - 1));
                    if (firstRow >= 0 && lastRow >= firstRow) {
                        pageSize = lastRow - firstRow + 1;
                    }
                }
            }
        }
        pageSize = std::clamp(pageSize, 1, 31);
        for (int step = 0; step < pageSize; ++step) {
            workspace->nextCard();
        }
        return;
    }
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
    updateCommandState();
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

    const QVector<DeckSortKey>& currentSortKeys = workspace->deck().sortKeys();
    const int sortFieldControls[] = {
        Control::SortFieldLevel1,
        Control::SortFieldLevel2,
        Control::SortFieldLevel3,
    };
    const int sortReverseControls[] = {
        Control::SortReverseLevel1,
        Control::SortReverseLevel2,
        Control::SortReverseLevel3,
    };
    for (int level = 0; level < 3; ++level) {
        const bool populated = level < currentSortKeys.size();
        if (auto* combo = uiControl<QComboBox>(*dialog, sortFieldControls[level])) {
            combo->setCurrentIndex(populated ? currentSortKeys.at(level).fieldIndex + 1 : 0);
        }
        setChecked(*dialog, sortReverseControls[level], populated && currentSortKeys.at(level).descending);
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
        handlePrintReportCommand();
        return;
    }

    fitNewReportFramesToForm(&report);

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
        const QListWidgetItem* selectedReport = reportList == nullptr ? nullptr : reportList->currentItem();
        reportIndex = selectedReport == nullptr
            ? -1
            : selectedReport->data(UiBuilder::ReportSourceIndexRole).toInt();
        if (reportAction == QStringLiteral("new")) {
            ReportDefinition newReport =
                createDefaultReportDefinition(workspace->deck(), tr("Untitled Report"));
            if (!configureReportForm(&newReport)) {
                continue;
            }
            fitNewReportFramesToForm(&newReport);
            openReportDesigner(workspace, -1, newReport);
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
    auto printer = m_printer;
    printer->setFullPage(true);
    if (report.formWidth > report.formHeight) {
        printer->setPageOrientation(QPageLayout::Landscape);
    }

    const auto buildRecords = [deckSnapshot, report, currentCardIndex](const QDialog& dialog) {
        return previewDataForDeck(deckSnapshot, report, printScopeFromDialog(dialog), currentCardIndex);
    };

    const auto previewSelectedScope = [this, printContext, report, buildRecords](const QDialog& dialog) {
        const QVector<ReportPreviewData> records = buildRecords(dialog);
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records);
        if (records.isEmpty() || pages.isEmpty()) {
            QMessageBox::information(this, tr("CardStack Reports"), tr("No cards are available for this report."));
            return false;
        }
        return ReportPreviewDialog::exec(this, printContext, report, records, pages) == QDialog::Accepted;
    };

    if (auto* previewButton = uiControl<QAbstractButton>(*optionsDialog, Control::PrintPreview)) {
        connect(previewButton, &QAbstractButton::clicked, optionsDialog.get(), [previewSelectedScope, dialog = optionsDialog.get()]() {
            if (previewSelectedScope(*dialog)) {
                dialog->accept();
            }
        });
    }
    if (auto* setupButton = uiControl<QAbstractButton>(*optionsDialog, Control::PrintPrinterSetup)) {
        connect(setupButton, &QAbstractButton::clicked, optionsDialog.get(), [this, printer]() {
            printer->setFullPage(true);
            showPrinterSetupDialog(printer.get(), this);
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
    const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records);
    if (records.isEmpty() || pages.isEmpty()) {
        QMessageBox::information(this, tr("CardStack Reports"), tr("No cards are available for this report."));
        return;
    }

    printer->setCopyCount(std::clamp(
        positiveEditValue(*optionsDialog, Control::PrintCopyCount, 1),
        1,
        10000));

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
        const QListWidgetItem* selectedReport = list == nullptr ? nullptr : list->currentItem();
        const int reportIndex = selectedReport == nullptr
            ? 0
            : selectedReport->data(UiBuilder::ReportSourceIndexRole).toInt();
        if (reportIndex < owner->deck().reportCount()) {
            openReportDesigner(owner, reportIndex, owner->deck().reportAt(reportIndex));
        }
        return;
    }
    case Command::FileCloseDeck:
        if (owner != nullptr) {
            const QList<QMdiSubWindow*> windows = m_mdiArea->subWindowList(QMdiArea::CreationOrder);
            for (QMdiSubWindow* window : windows) {
                auto* reportDesigner = window == nullptr ? nullptr : qobject_cast<ReportDesignerWidget*>(window->widget());
                if (reportDesigner == nullptr
                    || reportDesigner->property("ownerDeckWorkspace").value<QObject*>() != owner) {
                    continue;
                }
                QPointer<QMdiSubWindow> guard(window);
                window->close();
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                if (guard != nullptr && m_mdiArea->subWindowList().contains(guard.data())) {
                    return;
                }
            }
            for (QMdiSubWindow* window : m_mdiArea->subWindowList()) {
                if (window != nullptr && window->widget() == owner) {
                    window->close();
                    break;
                }
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
        designer->undo();
        return;
    case Command::EditCut:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->cutSelectedFrame();
        return;
    case Command::EditCopy:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->copySelectedFrame();
        return;
    case Command::EditPaste:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->pasteFrame();
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
    {
        const ReportFrameDefinition* selected = designer->selectedFrameDefinition();
        if (selected == nullptr) {
            statusBar()->showMessage(tr("Select a report frame first."), StatusMessageTimeoutMs);
            return;
        }
        const ReportFrameDefinition frame = *selected;
        std::unique_ptr<QDialog> dialog;
        if (frame.kind == ReportFrameKind::Text) {
            dialog = UiBuilder::createDialog(QStringLiteral("TEXTFRAME"), this, dialogContext());
            if (dialog) {
                setEditText(*dialog, Control::FrameText, frame.text);
                initializeFrameStyleDialog(*dialog, frame.styleFlags);
            }
        } else if (frame.kind == ReportFrameKind::Data) {
            dialog = UiBuilder::createDialog(QStringLiteral("DATAFRAME"), this, dialogContext());
            if (dialog) {
                if (auto* fields = uiControl<QComboBox>(*dialog, Control::DataFrameFieldList)) {
                    fields->clear();
                    fields->addItems(designer->fieldNames());
                    const int currentIndex = fields->findText(frame.fieldPlaceholders.value(0), Qt::MatchFixedString);
                    if (currentIndex >= 0) {
                        fields->setCurrentIndex(currentIndex);
                    }
                }
                setChecked(*dialog, Control::DataFramePrintEntireContents, frame.printEntireContentsFlag != 0);
                initializeFrameStyleDialog(*dialog, frame.styleFlags);
            }
        } else if (frame.kind == ReportFrameKind::SystemText) {
            dialog = UiBuilder::createDialog(QStringLiteral("ADDSYSTEMBOX"), this, dialogContext());
            if (dialog) {
                initializeSystemBoxDialog(*dialog);
                initializeSystemBoxDialogToken(*dialog, frame.text);
                initializeFrameStyleDialog(*dialog, frame.styleFlags);
            }
        } else if (frame.kind == ReportFrameKind::LineOrBox) {
            dialog = UiBuilder::createDialog(QStringLiteral("LINEFRAME"), this, dialogContext());
            if (dialog) {
                initializeLineFrameDialog(*dialog, frame.lineBoxShape, frame.lineStyle, frame.fillPattern, frame.cornerRadius);
            }
        }
        if (!dialog || dialog->exec() != QDialog::Accepted) {
            return;
        }
        if (frame.kind == ReportFrameKind::LineOrBox) {
            int shape = ReportLineShapeBox;
            if (isChecked(*dialog, Control::LineFrameHorizontal)) {
                shape = ReportLineShapeHorizontal;
            } else if (isChecked(*dialog, Control::LineFrameVertical)) {
                shape = ReportLineShapeVertical;
            }
            designer->updateSelectedFrameFromToolbar(
                frame.text, frame.styleFlags, frame.printEntireContentsFlag != 0, shape,
                comboIndex(*dialog, Control::LineFrameLineStyle),
                comboIndex(*dialog, Control::LineFrameFillPattern),
                positiveEditValue(*dialog, Control::LineFrameCornerRadius, 0));
        } else {
            QString text = frame.text;
            if (frame.kind == ReportFrameKind::Text) {
                text = editText(*dialog, Control::FrameText);
            } else if (frame.kind == ReportFrameKind::Data) {
                text = QStringLiteral("[%1]").arg(comboText(*dialog, Control::DataFrameFieldList));
            } else if (frame.kind == ReportFrameKind::SystemText) {
                text = tokenForSystemBoxDialog(*dialog);
            }
            designer->updateSelectedFrameFromToolbar(
                text,
                frameStyleFlagsFromDialog(*dialog),
                frame.kind == ReportFrameKind::Data && isChecked(*dialog, Control::DataFramePrintEntireContents),
                frame.lineBoxShape, frame.lineStyle, frame.fillPattern, frame.cornerRadius);
        }
        return;
    }
    case Command::ConfigureDataFont:
    case Command::ConfigureTextFont: {
        const bool dataFont = commandId == Command::ConfigureDataFont;
        const ReportFontDefinition current = dataFont ? designer->report().dataFont : designer->report().textFont;
        QFont initial(current.faceName);
        if (current.legacyHeight != 0) {
            initial.setPointSize(std::max(1, std::abs(current.legacyHeight)));
        }
        bool accepted = false;
        const QFont selected = QFontDialog::getFont(
            &accepted,
            initial,
            this,
            dataFont ? tr("Report Data Font") : tr("Report Text Font"));
        if (accepted) {
            designer->setReportFont(dataFont, {selected.family(), -std::max(1, selected.pointSize())});
        }
        return;
    }
    case Command::ConfigureColors:
        statusBar()->showMessage(tr("Report colors are controlled by individual frame attributes."), StatusMessageTimeoutMs);
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
            report.verticalGutter,
            report.headerHeight,
            report.footerHeight);
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
    case Command::ConfigureDeckDescription:
        handleDeckDescriptionCommand();
        return;
    case Command::EditUndo:
        designer->undo();
        return;
    case Command::EditCut:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->cutSelectedFrame();
        return;
    case Command::EditCopy:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->copySelectedFrame();
        return;
    case Command::EditPaste:
        if (dispatchFocusedTextClipboardCommand(commandId)) {
            return;
        }
        designer->pasteFrame();
        return;
    case Command::EditClear:
        designer->deleteSelectedFrame();
        return;
    case Command::ToolAddText:
    {
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TPLTEXTFRAME"), this, dialogContext());
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
            if (auto* fieldList = uiControl<QComboBox>(*dialog, Control::DataFrameFieldList)) {
                fieldList->clear();
                fieldList->addItems(designer->fieldNames());
            }
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
    {
        const CardTemplateFrame* selected = designer->selectedFrameDefinition();
        if (selected == nullptr) {
            statusBar()->showMessage(tr("Select a template frame first."), StatusMessageTimeoutMs);
            return;
        }
        const CardTemplateFrame frame = *selected;
        if (frame.kind == CardTemplateFrameKind::Text) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TPLTEXTFRAME"), this, dialogContext());
            if (!dialog) {
                return;
            }
            setEditText(*dialog, Control::FrameText, frame.text);
            initializeFrameStyleDialog(*dialog, frame.styleFlags);
            if (dialog->exec() == QDialog::Accepted) {
                designer->updateSelectedFrameFromToolbar(
                    editText(*dialog, Control::FrameText), frameStyleFlagsFromDialog(*dialog),
                    frame.lineBoxShape, frame.lineStyle, frame.fillPattern, frame.cornerRadius);
            }
            return;
        }
        if (frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox) {
            const int fieldIndex = designer->selectedFieldIndex();
            if (fieldIndex < 0 || fieldIndex >= designer->fieldDefinitions().size()) {
                return;
            }
            const FieldDefinition field = designer->fieldDefinitions().at(fieldIndex);
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TPLDATAFRAME"), this, dialogContext());
            if (!dialog) {
                return;
            }
            setEditText(*dialog, Control::FrameText, field.name());
            setEditText(*dialog, Control::TemplateFieldLength, QString::number(field.maxLength()));
            setChecked(*dialog, Control::TemplateFieldPhone, field.isPhone());
            setChecked(*dialog, Control::TemplateFieldShowName, field.showName());
            if (dialog->exec() == QDialog::Accepted) {
                designer->updateSelectedFieldDefinition(
                    editText(*dialog, Control::FrameText),
                    positiveEditValue(*dialog, Control::TemplateFieldLength, field.maxLength()),
                    isChecked(*dialog, Control::TemplateFieldPhone),
                    isChecked(*dialog, Control::TemplateFieldShowName));
            }
            return;
        }
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("LINEFRAME"), this, dialogContext());
        if (!dialog) {
            return;
        }
        initializeLineFrameDialog(
            *dialog,
            frame.lineBoxShape == CardTemplateLineBoxShape::HorizontalLine
                ? ReportLineShapeHorizontal
                : (frame.lineBoxShape == CardTemplateLineBoxShape::VerticalLine ? ReportLineShapeVertical : ReportLineShapeBox),
            frame.lineStyle, frame.fillPattern, frame.cornerRadius);
        if (dialog->exec() == QDialog::Accepted) {
            CardTemplateLineBoxShape shape = CardTemplateLineBoxShape::Box;
            if (isChecked(*dialog, Control::LineFrameHorizontal)) {
                shape = CardTemplateLineBoxShape::HorizontalLine;
            } else if (isChecked(*dialog, Control::LineFrameVertical)) {
                shape = CardTemplateLineBoxShape::VerticalLine;
            }
            designer->updateSelectedFrameFromToolbar(
                frame.text, frame.styleFlags, shape,
                comboIndex(*dialog, Control::LineFrameLineStyle),
                comboIndex(*dialog, Control::LineFrameFillPattern),
                positiveEditValue(*dialog, Control::LineFrameCornerRadius, 0));
        }
        return;
    }
    case Command::ConfigureDataFont:
    case Command::ConfigureNameFont:
    case Command::ConfigureTextFont:
    case Command::ConfigureIndexFont: {
        auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
        if (owner == nullptr) {
            statusBar()->showMessage(tr("Save the new template before changing template fonts."), StatusMessageTimeoutMs);
            return;
        }
        bool accepted = false;
        const DeckAppearance& appearance = owner->deck().appearance();
        const QString serialized = commandId == Command::ConfigureDataFont
            ? appearance.dataFont
            : commandId == Command::ConfigureNameFont
                ? appearance.nameFont
                : commandId == Command::ConfigureTextFont ? appearance.textFont : appearance.indexFont;
        const QFont selected = QFontDialog::getFont(
            &accepted, fontForDeckRole(serialized), this, tr("Template Font"));
        if (!accepted) {
            return;
        }
        if (commandId == Command::ConfigureDataFont) {
            owner->applyDataFont(selected);
        } else if (commandId == Command::ConfigureNameFont) {
            owner->applyNameFont(selected);
        } else if (commandId == Command::ConfigureTextFont) {
            owner->applyTextFont(selected);
        } else {
            owner->applyIndexFont(selected);
        }
        designer->setAppearance(owner->deck().appearance());
        return;
    }
    case Command::ConfigureColors: {
        auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
        if (owner == nullptr) {
            statusBar()->showMessage(tr("Save the new template before changing template colors."), StatusMessageTimeoutMs);
            return;
        }
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"), this, dialogContext());
        if (!dialog) {
            return;
        }
        DeckAppearance appearance = owner->deck().appearance();
        QStringList colors;
        for (const QString& color : appearance.customColors) {
            colors.append(color);
        }
        UiBuilder::setColorDialogState(dialog.get(), colors, appearance.useSystemColors);
        if (dialog->exec() != QDialog::Accepted) {
            return;
        }
        appearance.customColors.clear();
        for (const QString& color : UiBuilder::colorDialogColors(dialog.get())) {
            appearance.customColors.append(color);
        }
        appearance.useSystemColors = UiBuilder::colorDialogUsesSystemColors(dialog.get());
        owner->applyAppearance(std::move(appearance));
        return;
    }
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
            updateCommandState();
            return;
        }

        if (!workspace->securityPasswordMatches(password)) {
            QMessageBox::warning(this, tr("CardStack"), tr("ACCESS DENIED: Wrong Password"));
            continue;
        }

        workspace->clearSecurity();
        statusBar()->showMessage(tr("Deck security removed."), StatusMessageTimeoutMs);
        updateCommandState();
        return;
    }
}

void MainWindow::handleDeckDescriptionCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    const bool editingTemplate = workspace == nullptr && activeTemplateDesigner() != nullptr;
    if (editingTemplate) {
        workspace = qobject_cast<DeckWorkspace*>(
            activeTemplateDesigner()->property("ownerDeckWorkspace").value<QObject*>());
    }
    if (workspace == nullptr) {
        statusBar()->showMessage(tr("No active deck."), StatusMessageTimeoutMs);
        return;
    }

    UiBuilder::DialogContext context = dialogContext();
    context.deckName = deckDescriptionDisplayPath(workspace);
    context.deckDescription = workspace->deck().description();
    std::unique_ptr<QDialog> dialog =
        UiBuilder::createDialog(QStringLiteral("CHANGEDESC"), this, context);
    if (!dialog) {
        statusBar()->showMessage(tr("Deck description dialog is not available."), StatusMessageTimeoutMs);
        return;
    }

    if (dialog->exec() != QDialog::Accepted) {
        return;
    }

    const auto* description = uiControl<QLineEdit>(*dialog, Control::DeckDescriptionText);
    workspace->setDeckDescription(description == nullptr ? QString() : description->text());
    statusBar()->showMessage(
        editingTemplate ? tr("Template description changed.") : tr("Deck description changed."),
        StatusMessageTimeoutMs);
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
        if (workspace != nullptr && isChecked(*dialog, Control::PhoneLogCall)) {
            workspace->commitPendingEdits();
            PhoneCallLogEntry entry;
            entry.calledAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            entry.phoneNumber = number;
            const int cardIndex = workspace->currentCardIndex();
            if (cardIndex >= 0 && cardIndex < workspace->deck().cardCount()) {
                const CardRecord& card = workspace->deck().cardAt(cardIndex);
                for (int index = 0; index < 3; ++index) {
                    entry.cardSummaryValues.append(card.valueAt(index));
                }
            }
            workspace->appendPhoneCallLogEntry(std::move(entry));

            const QString filePath = activeDeckPath();
            if (!filePath.isEmpty()) {
                QString error;
                if (SQLitePackageStore::saveDeckPackage(workspace->deck(), filePath, &error)) {
                    workspace->clearDirty();
                } else {
                    QMessageBox::warning(
                        this,
                        tr("Phone Call Log"),
                        tr("The call remains in the open deck, but its call log could not be saved:\n%1").arg(error));
                }
            }
        }
        statusBar()->showMessage(tr("Phone number copied and sent to the system phone handler."), StatusMessageTimeoutMs);
    } else {
        QMessageBox::information(
            this,
            tr("Dial Phone Number"),
            tr("The phone number was copied to the clipboard, but no system phone handler accepted the tel: link."));
    }
}

void MainWindow::handlePhoneCallLogCommand()
{
    DeckWorkspace* workspace = activeDeckWorkspace();
    if (workspace == nullptr) {
        QMessageBox::information(this, tr("Phone Call Log"), tr("Open a deck to view its phone call log."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Phone Call Log - %1").arg(workspace->deck().name()));
    dialog.resize(900, 420);
    auto* layout = new QVBoxLayout(&dialog);

    auto* guidance = new QLabel(
        tr("Calls recorded for this deck. Select one or more rows to delete them, or import and export legacy .LOG files."),
        &dialog);
    guidance->setWordWrap(true);
    layout->addWidget(guidance);

    constexpr int SummaryColumnCount = 3;
    auto* table = new QTableWidget(workspace->deck().phoneCallLogEntryCount(), 5, &dialog);
    table->setObjectName(QStringLiteral("phoneCallLogTable"));
    QStringList headers{tr("Called"), tr("Phone number")};
    for (int index = 0; index < SummaryColumnCount; ++index) {
        const QString fieldName = index < workspace->deck().fields().size()
            ? workspace->deck().fields().at(index).name().trimmed()
            : QString();
        headers.append(fieldName.isEmpty() ? tr("Card detail %1").arg(index + 1) : fieldName);
    }
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    auto* actionLayout = new QHBoxLayout;
    auto* importButton = new QPushButton(tr("&Import .LOG..."), &dialog);
    auto* exportButton = new QPushButton(tr("&Export .LOG..."), &dialog);
    auto* deleteButton = new QPushButton(tr("&Delete Selected"), &dialog);
    auto* closeButton = new QPushButton(tr("&Close"), &dialog);
    importButton->setObjectName(QStringLiteral("phoneCallLogImportButton"));
    exportButton->setObjectName(QStringLiteral("phoneCallLogExportButton"));
    deleteButton->setObjectName(QStringLiteral("phoneCallLogDeleteButton"));
    closeButton->setObjectName(QStringLiteral("phoneCallLogCloseButton"));
    deleteButton->setShortcut(QKeySequence::Delete);
    closeButton->setDefault(true);
    actionLayout->addWidget(importButton);
    actionLayout->addWidget(exportButton);
    actionLayout->addStretch(1);
    actionLayout->addWidget(deleteButton);
    actionLayout->addWidget(closeButton);
    layout->addLayout(actionLayout);

    const auto updateActionState = [table, workspace, exportButton, deleteButton]() {
        const bool hasEntries = workspace->deck().phoneCallLogEntryCount() > 0;
        exportButton->setEnabled(hasEntries);
        deleteButton->setEnabled(
            hasEntries && table->selectionModel() != nullptr && !table->selectionModel()->selectedRows().isEmpty());
    };

    const auto refreshTable = [this, table, workspace, updateActionState]() {
        const QVector<PhoneCallLogEntry>& entries = workspace->deck().phoneCallLogEntries();
        table->clearContents();
        table->setRowCount(entries.size());
        for (int row = 0; row < entries.size(); ++row) {
            const PhoneCallLogEntry& entry = entries.at(row);
            const QDateTime calledAt = QDateTime::fromString(entry.calledAtUtc, Qt::ISODateWithMs).toLocalTime();
            const QString calledText = calledAt.isValid()
                ? QLocale().toString(calledAt, QLocale::ShortFormat)
                : tr("Legacy record");
            table->setItem(row, 0, new QTableWidgetItem(calledText));
            table->setItem(row, 1, new QTableWidgetItem(entry.phoneNumber));
            for (int index = 0; index < SummaryColumnCount; ++index) {
                table->setItem(row, index + 2, new QTableWidgetItem(
                    index < entry.cardSummaryValues.size() ? entry.cardSummaryValues.at(index) : QString()));
            }
        }
        table->clearSelection();
        updateActionState();
    };
    refreshTable();
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &dialog, updateActionState);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(importButton, &QPushButton::clicked, &dialog, [this, workspace, refreshTable]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this, tr("Import Phone Call Log"), QString(), tr("Legacy phone call logs (*.LOG *.log);;All files (*)"));
        if (filePath.isEmpty()) {
            return;
        }

        Deck importedLog;
        QString warning;
        const int parsed = PhoneCallLog::importLegacyFile(filePath, &importedLog, &warning);
        if (!warning.isEmpty()) {
            QMessageBox::warning(this, tr("Phone Call Log"), warning);
            return;
        }

        workspace->commitPendingEdits();
        int added = 0;
        QVector<PhoneCallLogEntry> knownEntries = workspace->deck().phoneCallLogEntries();
        for (const PhoneCallLogEntry& entry : importedLog.phoneCallLogEntries()) {
            const bool duplicate = std::any_of(
                knownEntries.cbegin(),
                knownEntries.cend(),
                [&entry](const PhoneCallLogEntry& known) {
                    if (!entry.rawLegacyBytes.isEmpty() && entry.rawLegacyBytes == known.rawLegacyBytes) {
                        return true;
                    }
                    if (entry.phoneNumber != known.phoneNumber
                        || entry.cardSummaryValues != known.cardSummaryValues) {
                        return false;
                    }
                    const QDateTime importedTime = QDateTime::fromString(entry.calledAtUtc, Qt::ISODateWithMs);
                    const QDateTime knownTime = QDateTime::fromString(known.calledAtUtc, Qt::ISODateWithMs);
                    return importedTime.isValid() && knownTime.isValid()
                        && importedTime.toSecsSinceEpoch() / 60 == knownTime.toSecsSinceEpoch() / 60;
                });
            if (!duplicate) {
                workspace->appendPhoneCallLogEntry(entry);
                knownEntries.append(entry);
                ++added;
            }
        }

        if (added > 0) {
            const QString deckPath = activeDeckPath();
            if (!deckPath.isEmpty()) {
                QString error;
                if (SQLitePackageStore::saveDeckPackage(workspace->deck(), deckPath, &error)) {
                    workspace->clearDirty();
                } else {
                    QMessageBox::warning(
                        this,
                        tr("Phone Call Log"),
                        tr("The imported calls remain in the open deck, but could not be saved:\n%1").arg(error));
                }
            }
            refreshTable();
        }

        QMessageBox::information(
            this,
            tr("Phone Call Log"),
            parsed == 0
                ? tr("The selected file did not contain any call-log records.")
                : added > 0
                ? tr("Imported %1 call-log record(s).%2")
                      .arg(added)
                      .arg(parsed > added ? tr(" %1 duplicate(s) were skipped.").arg(parsed - added) : QString())
                : tr("No new call-log records were found; all %1 record(s) were already present.").arg(parsed));
    });
    connect(exportButton, &QPushButton::clicked, &dialog, [this, workspace]() {
        QString suggested = activeDeckPath();
        if (suggested.isEmpty()) {
            suggested = workspace->deck().name();
        }
        suggested = QFileInfo(suggested).absolutePath() + QDir::separator()
            + QFileInfo(suggested).completeBaseName() + QStringLiteral(".LOG");
        const QString filePath = QFileDialog::getSaveFileName(
            this, tr("Export Phone Call Log"), suggested, tr("Legacy phone call logs (*.LOG *.log)"));
        if (filePath.isEmpty()) {
            return;
        }
        QString error;
        if (!PhoneCallLog::writeLegacyFile(workspace->deck(), filePath, &error)) {
            QMessageBox::critical(this, tr("Phone Call Log"), tr("Could not export the call log:\n%1").arg(error));
        }
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [this, table, workspace, refreshTable]() {
        const QModelIndexList selectedRows = table->selectionModel()->selectedRows();
        if (selectedRows.isEmpty()) {
            return;
        }

        const int selectedCount = selectedRows.size();
        QMessageBox confirmation(
            QMessageBox::Question,
            tr("Delete Call Log Entries"),
            selectedCount == 1
                ? tr("Delete the selected call-log entry?")
                : tr("Delete the %1 selected call-log entries?").arg(selectedCount),
            QMessageBox::NoButton,
            this);
        QPushButton* confirmDeleteButton = confirmation.addButton(tr("&Delete"), QMessageBox::DestructiveRole);
        QPushButton* cancelButton = confirmation.addButton(QMessageBox::Cancel);
        confirmation.setDefaultButton(cancelButton);
        confirmation.exec();
        if (confirmation.clickedButton() != confirmDeleteButton) {
            return;
        }

        workspace->commitPendingEdits();
        QVector<int> entryIndexes;
        entryIndexes.reserve(selectedRows.size());
        for (const QModelIndex& index : selectedRows) {
            entryIndexes.append(index.row());
        }

        const int removed = workspace->removePhoneCallLogEntries(entryIndexes);
        if (removed == 0) {
            return;
        }

        const QString deckPath = activeDeckPath();
        if (!deckPath.isEmpty()) {
            QString error;
            if (SQLitePackageStore::saveDeckPackage(workspace->deck(), deckPath, &error)) {
                workspace->clearDirty();
            } else {
                QMessageBox::warning(
                    this,
                    tr("Phone Call Log"),
                    tr("The calls remain deleted in the open deck, but the changes could not be saved:\n%1").arg(error));
            }
        }
        refreshTable();
    });
    dialog.exec();
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
    savePhoneDialerSettings();
    statusBar()->showMessage(tr("Phone dialer settings updated."), StatusMessageTimeoutMs);
}

void MainWindow::loadPhoneDialerSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("phoneDialer"));
    const int settingsVersion = settings.value(QStringLiteral("settingsVersion"), 0).toInt();
    m_phoneUseLongDistance = settings.value(QStringLiteral("useLongDistance"), m_phoneUseLongDistance).toBool();
    m_phoneGetOutsideLine = settings.value(QStringLiteral("getOutsideLine"), m_phoneGetOutsideLine).toBool();
    m_phoneLogCalls = settings.value(QStringLiteral("logCalls"), m_phoneLogCalls).toBool();
    m_phoneLongDistancePrefix = settings.value(QStringLiteral("longDistancePrefix"), m_phoneLongDistancePrefix).toString();
    m_phoneOutsideLinePrefix = settings.value(QStringLiteral("outsideLinePrefix"), m_phoneOutsideLinePrefix).toString();
    m_phoneLocalAreaCode = settings.value(QStringLiteral("localAreaCode"), m_phoneLocalAreaCode).toString();

    const QJsonDocument document = QJsonDocument::fromJson(settings.value(QStringLiteral("quickDials")).toByteArray());
    if (document.isArray()) {
        QVector<QuickDial> quickDials;
        for (const QJsonValue& value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString phoneNumber = object.value(QStringLiteral("phoneNumber")).toString();
            if (!phoneNumber.isEmpty()) {
                quickDials.append({object.value(QStringLiteral("description")).toString(), phoneNumber});
            }
        }
        m_quickDials = std::move(quickDials);
    }
    if (settingsVersion < 2
        && m_quickDials.size() == 2
        && m_quickDials.at(0).phoneNumber == QStringLiteral("0")
        && m_quickDials.at(1).phoneNumber == QStringLiteral("411")) {
        m_quickDials.clear();
    }
    settings.endGroup();
}

void MainWindow::savePhoneDialerSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("phoneDialer"));
    settings.setValue(QStringLiteral("settingsVersion"), 2);
    settings.setValue(QStringLiteral("useLongDistance"), m_phoneUseLongDistance);
    settings.setValue(QStringLiteral("getOutsideLine"), m_phoneGetOutsideLine);
    settings.setValue(QStringLiteral("logCalls"), m_phoneLogCalls);
    settings.setValue(QStringLiteral("longDistancePrefix"), m_phoneLongDistancePrefix);
    settings.setValue(QStringLiteral("outsideLinePrefix"), m_phoneOutsideLinePrefix);
    settings.setValue(QStringLiteral("localAreaCode"), m_phoneLocalAreaCode);
    QJsonArray quickDials;
    for (const QuickDial& quickDial : m_quickDials) {
        quickDials.append(QJsonObject{
            {QStringLiteral("description"), quickDial.description},
            {QStringLiteral("phoneNumber"), quickDial.phoneNumber},
        });
    }
    settings.setValue(QStringLiteral("quickDials"), QJsonDocument(quickDials).toJson(QJsonDocument::Compact));
    settings.endGroup();
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
    if (list != nullptr) {
        QRect listGeometry = list->geometry();
        const int originalListHeight = listGeometry.height();
        listGeometry.setHeight(std::max(listGeometry.height(), ReportFormListMinimumHeightPx));
        list->setGeometry(listGeometry);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        const int heightGrowth = listGeometry.height() - originalListHeight;
        if (heightGrowth > 0) {
            for (QGroupBox* group : dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (group->geometry().contains(listGeometry.topLeft())) {
                    QRect groupGeometry = group->geometry();
                    groupGeometry.setHeight(groupGeometry.height() + heightGrowth);
                    group->setGeometry(groupGeometry);
                    break;
                }
            }
            dialog->resize(dialog->width(), dialog->height() + heightGrowth);
            dialog->setMinimumHeight(dialog->height());
        }
    }
    const auto populateList = [this, list](ReportFormType type) {
        if (list == nullptr) {
            return;
        }
        list->clear();
        const QVector<ReportFormPreset> presets = reportFormPresets(type);
        QVector<int> presetOrder;
        presetOrder.reserve(presets.size());
        for (int index = 0; index < presets.size(); ++index) {
            presetOrder.append(index);
        }
        std::sort(
            presetOrder.begin(),
            presetOrder.end(),
            [this, &presets](int left, int right) {
                const QString leftName =
                    presets.at(left).label.section(QLatin1Char('\t'), 0, 0);
                const QString rightName =
                    presets.at(right).label.section(QLatin1Char('\t'), 0, 0);
                return QString::localeAwareCompare(leftName, rightName) < 0;
            });
        int firstColumnWidth = 0;
        int secondColumnWidth = 0;
        int thirdColumnWidth = 0;
        for (const ReportFormPreset& preset : presets) {
            const QString label = preset.label;
            firstColumnWidth = std::max(
                firstColumnWidth,
                list->fontMetrics().horizontalAdvance(label.section(QLatin1Char('\t'), 0, 0)));
            secondColumnWidth = std::max(
                secondColumnWidth,
                list->fontMetrics().horizontalAdvance(label.section(QLatin1Char('\t'), 1, 1).trimmed()));
            thirdColumnWidth = std::max(
                thirdColumnWidth,
                list->fontMetrics().horizontalAdvance(label.section(QLatin1Char('\t'), 2, -1).simplified()));
        }
        for (int displayIndex = 0; displayIndex < presetOrder.size(); ++displayIndex) {
            const int index = presetOrder.at(displayIndex);
            const QString label = presets.at(index).label;
            auto* item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, index);
            item->setData(Qt::AccessibleTextRole, label);
            if (label.contains(QLatin1Char('\t'))) {
                item->setSizeHint(QSize(firstColumnWidth + secondColumnWidth + thirdColumnWidth + 40,
                                        list->fontMetrics().height() + 8));
                auto* row = new QWidget(list);
                auto* rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(4, 0, 4, 0);
                rowLayout->setSpacing(12);
                auto* name = new QLabel(label.section(QLatin1Char('\t'), 0, 0), row);
                name->setFixedWidth(firstColumnWidth);
                auto* size = new QLabel(label.section(QLatin1Char('\t'), 1, 1).trimmed(), row);
                size->setFixedWidth(secondColumnWidth);
                auto* qualifier = new QLabel(label.section(QLatin1Char('\t'), 2, -1).simplified(), row);
                qualifier->setMinimumWidth(thirdColumnWidth);
                rowLayout->addWidget(name);
                rowLayout->addWidget(size);
                rowLayout->addWidget(qualifier, 1);
                list->setItemWidget(item, row);
            } else {
                item->setText(label);
            }
        }
        if (list->count() > 0) {
            list->setCurrentRow(0);
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
        const int requiredWidth = customButton->fontMetrics().horizontalAdvance(customButton->text()) + 16;
        if (customButton->width() < requiredWidth) {
            QRect geometry = customButton->geometry();
            geometry.setLeft(std::max(0, geometry.right() - requiredWidth + 1));
            customButton->setGeometry(geometry);
        }
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

    const QVector<ReportFormPreset> selectedPresets = reportFormPresets(selectedType);
    int selectedPresetIndex = 0;
    if (list != nullptr && list->currentItem() != nullptr) {
        selectedPresetIndex = list->currentItem()->data(Qt::UserRole).toInt();
    }
    if (selectedPresetIndex < 0 || selectedPresetIndex >= selectedPresets.size()) {
        selectedPresetIndex = 0;
    }
    if (selectedPresets.isEmpty()) {
        return false;
    }
    const ReportFormPreset selectedPreset = selectedPresets.at(selectedPresetIndex);

    if (useCustomForm) {
        std::unique_ptr<QDialog> defineDialog = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"), this, dialogContext());
        if (!defineDialog) {
            statusBar()->showMessage(tr("Define Form dialog is not available."), StatusMessageTimeoutMs);
            return false;
        }

        setEditText(*defineDialog, Control::DefineFormWidth, QString::number(selectedPreset.formWidth / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormHeight, QString::number(selectedPreset.formHeight / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginLeft, QString::number(selectedPreset.marginLeft / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginTop, QString::number(selectedPreset.marginTop / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginRight, QString::number(selectedPreset.marginRight / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormMarginBottom, QString::number(selectedPreset.marginBottom / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormHorizontalGutter, QString::number(selectedPreset.horizontalGutter / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormVerticalGutter, QString::number(selectedPreset.verticalGutter / 1000.0, 'f', 3));
        setEditText(*defineDialog, Control::DefineFormRows,
                    selectedType == ReportFormType::Report
                        ? QString::number((report->headerHeight > 0 ? report->headerHeight : 72) / 96.0, 'f', 2)
                        : QString::number(std::max(1, selectedPreset.rows)));
        setEditText(*defineDialog, Control::DefineFormColumns,
                    selectedType == ReportFormType::Report
                        ? QString::number((report->footerHeight > 0 ? report->footerHeight : 72) / 96.0, 'f', 2)
                        : QString::number(std::max(1, selectedPreset.columns)));
        if (auto* paperSize = uiControl<QComboBox>(*defineDialog, Control::DefineFormPageSize)) {
            paperSize->clear();
            paperSize->addItem(
                selectedPreset.label.section(QLatin1Char('\t'), 0, 0), selectedPreset.paperStyleId);
            paperSize->setCurrentIndex(0);
        }
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
        setDefineEditWidth(Control::DefineFormRows, DefineFormMeasureEditWidthPx);
        setDefineEditWidth(Control::DefineFormColumns, DefineFormMeasureEditWidthPx);
        for (int controlId : {
                 Control::DefineFormComputedWidth,
                 Control::DefineFormComputedHeight,
             }) {
            if (auto* label = uiControl<QLabel>(*defineDialog, controlId)) {
                label->setMinimumWidth(250);
                label->resize(250, std::max(label->height(), label->fontMetrics().height() + 4));
            }
        }

        auto* defineFormGroup = new QButtonGroup(defineDialog.get());
        defineFormGroup->setExclusive(true);
        for (int controlId : {Control::DefineFormCard, Control::DefineFormLabel, Control::DefineFormReport}) {
            if (auto* button = uiControl<QAbstractButton>(*defineDialog, controlId)) {
                defineFormGroup->addButton(button, controlId);
            }
        }
        setChecked(*defineDialog, Control::DefineFormCard, selectedType == ReportFormType::Card);
        setChecked(*defineDialog, Control::DefineFormLabel, selectedType == ReportFormType::Label);
        setChecked(*defineDialog, Control::DefineFormReport, selectedType == ReportFormType::Report);

        auto* orientationGroup = new QButtonGroup(defineDialog.get());
        orientationGroup->setExclusive(true);
        for (int controlId : {Control::DefineFormPortrait, Control::DefineFormLandscape}) {
            if (auto* button = uiControl<QAbstractButton>(*defineDialog, controlId)) {
                orientationGroup->addButton(button, controlId);
            }
        }
        setChecked(*defineDialog,
                   selectedPreset.orientation != 0 || selectedPreset.formWidth > selectedPreset.formHeight
                       ? Control::DefineFormLandscape
                       : Control::DefineFormPortrait,
                   true);

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
            const int rows = multiCell ? positiveEditValue(*defineDialog, Control::DefineFormRows, 1) : 1;
            const int columns = multiCell ? positiveEditValue(*defineDialog, Control::DefineFormColumns, 1) : 1;

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

            setControlsEnabled(
                {
                    Control::DefineFormHorizontalGutter,
                    Control::DefineFormVerticalGutter,
                },
                multiCell);
            setControlsEnabled({Control::DefineFormRows, Control::DefineFormColumns}, true);

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
            currentDefineType,
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
                const bool reportDimension =
                    currentDefineType() == ReportFormType::Report
                    && (editControlId == Control::DefineFormRows
                        || editControlId == Control::DefineFormColumns);
                if (measure || reportDimension) {
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
            return configureReportForm(report);
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
        preset.rows = preset.type == ReportFormType::Report
            ? 1
            : positiveEditValue(*defineDialog, Control::DefineFormRows,
                                preset.type == ReportFormType::Label ? DefaultLabelRows : 1);
        preset.columns = preset.type == ReportFormType::Report
            ? 1
            : positiveEditValue(*defineDialog, Control::DefineFormColumns,
                                preset.type == ReportFormType::Label ? DefaultLabelColumns : 1);
        preset.marginLeft = formMeasureValue(*defineDialog, Control::DefineFormMarginLeft, 0);
        preset.marginTop = formMeasureValue(*defineDialog, Control::DefineFormMarginTop, 0);
        preset.marginRight = formMeasureValue(*defineDialog, Control::DefineFormMarginRight, 0);
        preset.marginBottom = formMeasureValue(*defineDialog, Control::DefineFormMarginBottom, 0);
        preset.horizontalGutter = formMeasureValue(*defineDialog, Control::DefineFormHorizontalGutter, 0);
        preset.verticalGutter = formMeasureValue(*defineDialog, Control::DefineFormVerticalGutter, 0);
        preset.pageWidth = preset.marginLeft + preset.marginRight
            + preset.columns * preset.formWidth
            + std::max(0, preset.columns - 1) * preset.horizontalGutter;
        preset.pageHeight = preset.marginTop + preset.marginBottom
            + preset.rows * preset.formHeight
            + std::max(0, preset.rows - 1) * preset.verticalGutter;
        preset.orientation = preset.pageWidth > preset.pageHeight ? 1 : 0;
        preset.paperStyleId = preset.type == ReportFormType::Card
            ? CardFormNameLastId + 1
            : preset.type == ReportFormType::Label
                ? LabelFormNameLastId + 1
                : ReportFormNameLastId + 1;
        applyReportFormPreset(report, preset);
        if (preset.type == ReportFormType::Report) {
            report->headerHeight = qRound(
                formMeasureValue(*defineDialog, Control::DefineFormRows, 750) * 96.0 / 1000.0);
            report->footerHeight = qRound(
                formMeasureValue(*defineDialog, Control::DefineFormColumns, 750) * 96.0 / 1000.0);
        }
        return true;
    }

    if (selectedPresets.isEmpty()) {
        return false;
    }

    applyReportFormPreset(report, selectedPresets.at(selectedPresetIndex));
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

    if ((isDelimitedTextPath(filePath) || isLegacyInterchangePath(filePath))
        && !reviewImportedDeck(filePath, &deck)) {
        statusBar()->showMessage(tr("Import canceled."), StatusMessageTimeoutMs);
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

bool MainWindow::reviewImportedDeck(const QString& filePath, Deck* deck)
{
    if (deck == nullptr || deck->fieldCount() <= 0) {
        return false;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("importExamineDialog"));
    dialog.setWindowTitle(tr("Examine Import - %1").arg(QFileInfo(filePath).fileName()));
    dialog.resize(920, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* instruction = new QLabel(
        tr("Review values, edit field definitions, or skip records before importing."), &dialog);
    instruction->setWordWrap(true);
    layout->addWidget(instruction);

    QVector<FieldDefinition> reviewedFields = deck->fields();
    auto* table = new QTableWidget(deck->cardCount(), deck->fieldCount() + 1, &dialog);
    table->setObjectName(QStringLiteral("importExamineTable"));
    QStringList headers{tr("Import")};
    for (const FieldDefinition& field : reviewedFields) {
        headers.append(field.name());
    }
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    for (int row = 0; row < deck->cardCount(); ++row) {
        auto* includeItem = new QTableWidgetItem(QString::number(row + 1));
        includeItem->setCheckState(Qt::Checked);
        includeItem->setFlags((includeItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        table->setItem(row, 0, includeItem);
        for (int fieldIndex = 0; fieldIndex < deck->fieldCount(); ++fieldIndex) {
            table->setItem(row, fieldIndex + 1, new QTableWidgetItem(deck->cardAt(row).valueAt(fieldIndex)));
        }
    }
    if (table->rowCount() > 0) {
        table->setCurrentCell(0, std::min(1, table->columnCount() - 1));
    }
    layout->addWidget(table, 1);

    auto* progression = new QHBoxLayout;
    auto* previous = new QPushButton(tr("Previous Record"), &dialog);
    auto* next = new QPushButton(tr("Next Record"), &dialog);
    auto* skip = new QPushButton(tr("Skip Record"), &dialog);
    auto* editField = new QPushButton(tr("Edit Field..."), &dialog);
    previous->setObjectName(QStringLiteral("importPreviousRecordButton"));
    next->setObjectName(QStringLiteral("importNextRecordButton"));
    skip->setObjectName(QStringLiteral("importSkipRecordButton"));
    editField->setObjectName(QStringLiteral("importEditFieldButton"));
    progression->addWidget(previous);
    progression->addWidget(next);
    progression->addWidget(skip);
    progression->addStretch(1);
    progression->addWidget(editField);
    layout->addLayout(progression);

    const auto selectRow = [table](int requestedRow) {
        if (table->rowCount() <= 0) {
            return;
        }
        const int row = std::clamp(requestedRow, 0, table->rowCount() - 1);
        table->setCurrentCell(row, std::clamp(table->currentColumn(), 1, table->columnCount() - 1));
        table->scrollToItem(table->currentItem());
    };
    connect(previous, &QPushButton::clicked, &dialog, [table, selectRow]() { selectRow(table->currentRow() - 1); });
    connect(next, &QPushButton::clicked, &dialog, [table, selectRow]() { selectRow(table->currentRow() + 1); });
    connect(skip, &QPushButton::clicked, &dialog, [table, selectRow]() {
        const int row = table->currentRow();
        if (row >= 0 && table->item(row, 0) != nullptr) {
            table->item(row, 0)->setCheckState(Qt::Unchecked);
            selectRow(row + 1);
        }
    });

    connect(editField, &QPushButton::clicked, &dialog, [this, &dialog, table, &reviewedFields]() {
        const int fieldIndex = table->currentColumn() - 1;
        if (fieldIndex < 0 || fieldIndex >= reviewedFields.size()) {
            return;
        }
        std::unique_ptr<QDialog> editor = UiBuilder::createDialog(QStringLiteral("IMPEDIT"), &dialog, dialogContext());
        if (!editor) {
            return;
        }
        const FieldDefinition original = reviewedFields.at(fieldIndex);
        auto* name = qobject_cast<QLineEdit*>(UiBuilder::controlById(editor.get(), 704));
        auto* sample = qobject_cast<QLineEdit*>(UiBuilder::controlById(editor.get(), 705));
        auto* length = qobject_cast<QLineEdit*>(UiBuilder::controlById(editor.get(), 706));
        auto* notes = qobject_cast<QCheckBox*>(UiBuilder::controlById(editor.get(), 707));
        if (name != nullptr) {
            name->setText(original.name());
        }
        if (sample != nullptr) {
            sample->setText(table->currentItem() == nullptr ? QString() : table->currentItem()->text());
        }
        if (length != nullptr) {
            length->setText(QString::number(original.maxLength()));
        }
        if (notes != nullptr) {
            notes->setChecked(original.isNotes());
        }
        if (editor->exec() != QDialog::Accepted) {
            return;
        }

        const QString revisedName = name == nullptr ? original.name() : name->text().trimmed();
        if (revisedName.isEmpty()) {
            return;
        }
        const bool revisedNotes = notes != nullptr && notes->isChecked();
        const int revisedLength = revisedNotes
            ? std::max(8192, original.maxLength())
            : std::clamp(length == nullptr ? original.maxLength() : length->text().toInt(), 1, 12192);
        if (revisedNotes) {
            for (int index = 0; index < reviewedFields.size(); ++index) {
                if (index != fieldIndex && reviewedFields.at(index).isNotes()) {
                    const FieldDefinition other = reviewedFields.at(index);
                    reviewedFields[index] = FieldDefinition(
                        other.name(), FieldType::Text, std::min(other.maxLength(), 256),
                        other.showName(), other.isPhone(), other.legacyDescriptor(), other.displayWidth());
                }
            }
        }
        reviewedFields[fieldIndex] = FieldDefinition(
            revisedName, revisedNotes ? FieldType::Notes : FieldType::Text, revisedLength,
            original.showName(), original.isPhone(), original.legacyDescriptor(), original.displayWidth());
        table->horizontalHeaderItem(fieldIndex + 1)->setText(revisedName);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("importReviewButtons"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QVector<CardRecord> reviewedCards;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) == nullptr || table->item(row, 0)->checkState() != Qt::Checked) {
            continue;
        }
        CardRecord card;
        for (int fieldIndex = 0; fieldIndex < reviewedFields.size(); ++fieldIndex) {
            const QTableWidgetItem* item = table->item(row, fieldIndex + 1);
            card.appendValue(item == nullptr ? QString() : item->text());
        }
        reviewedCards.append(std::move(card));
    }
    for (int fieldIndex = 0; fieldIndex < reviewedFields.size(); ++fieldIndex) {
        const FieldDefinition original = reviewedFields.at(fieldIndex);
        if (original.isNotes()) {
            continue;
        }
        int requiredLength = original.maxLength();
        for (const CardRecord& card : reviewedCards) {
            requiredLength = std::max(requiredLength, static_cast<int>(card.valueAt(fieldIndex).size()));
        }
        if (requiredLength != original.maxLength()) {
            reviewedFields[fieldIndex] = FieldDefinition(
                original.name(), original.type(), std::min(requiredLength, 12192),
                original.showName(), original.isPhone(), original.legacyDescriptor(), original.displayWidth());
        }
    }
    deck->setFields(std::move(reviewedFields));
    deck->setCards(std::move(reviewedCards));

    QVector<ImportExportProfile> profiles = deck->importExportProfiles();
    ImportExportProfile reviewProfile = delimitedTextProfileForPath(filePath, ImportExportProfileType::Import);
    reviewProfile.name = tr("Reviewed import: %1").arg(QFileInfo(filePath).fileName());
    reviewProfile.fieldMappings.clear();
    for (int fieldIndex = 0; fieldIndex < deck->fieldCount(); ++fieldIndex) {
        reviewProfile.fieldMappings.append(fieldIndex);
    }
    profiles.append(std::move(reviewProfile));
    deck->setImportExportProfiles(std::move(profiles));
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
    if (result.ok() && !result.warningMessages.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("CardStack Migration"),
            tr("The deck was imported, but some related data needs attention:\n%1")
                .arg(result.warningMessages.join(QLatin1Char('\n'))));
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
    templateDeck.setFields(designer->fieldDefinitions());
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

QByteArray MainWindow::captureDeckWindowSession() const
{
    QJsonArray windows;
    const DeckWorkspace* activeWorkspace = activeDeckWorkspace();
    for (QMdiSubWindow* subWindow : m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        const auto* workspace = subWindow == nullptr
            ? nullptr
            : qobject_cast<DeckWorkspace*>(subWindow->widget());
        if (workspace == nullptr) {
            continue;
        }

        const QString filePath = workspace->property("cardstackFilePath").toString().trimmed();
        if (filePath.isEmpty()) {
            continue;
        }

        QRect geometry = subWindow->isMinimized() || subWindow->isMaximized()
            ? subWindow->normalGeometry()
            : subWindow->geometry();
        if (!geometry.isValid()) {
            geometry = subWindow->geometry();
        }
        windows.append(QJsonObject{
            {QStringLiteral("path"), QFileInfo(filePath).absoluteFilePath()},
            {QStringLiteral("x"), geometry.x()},
            {QStringLiteral("y"), geometry.y()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
            {QStringLiteral("minimized"), subWindow->isMinimized()},
            {QStringLiteral("maximized"), subWindow->isMaximized()},
            {QStringLiteral("active"), workspace == activeWorkspace},
        });
    }
    return QJsonDocument(windows).toJson(QJsonDocument::Compact);
}

void MainWindow::saveWindowSession(const QByteArray& deckWindows)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(WindowSessionGroup));
    settings.setValue(QString::fromLatin1(WindowSessionVersionKey), CurrentWindowSessionVersion);
    settings.setValue(QString::fromLatin1(WindowSessionMainGeometryKey), saveGeometry());
    settings.setValue(QString::fromLatin1(WindowSessionMainStateKey), saveState());
    settings.setValue(QString::fromLatin1(WindowSessionDeckWindowsKey), deckWindows);
    settings.endGroup();
    settings.sync();
}

bool MainWindow::restoreWindowSession()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(WindowSessionGroup));
    const int version = settings.value(QString::fromLatin1(WindowSessionVersionKey), 0).toInt();
    if (version != CurrentWindowSessionVersion) {
        settings.endGroup();
        return false;
    }

    const QByteArray mainGeometry = settings.value(
        QString::fromLatin1(WindowSessionMainGeometryKey)).toByteArray();
    if (!mainGeometry.isEmpty() && restoreGeometry(mainGeometry)) {
        setProperty("cardstackMainGeometryRestored", true);
    }
    const QByteArray mainState = settings.value(
        QString::fromLatin1(WindowSessionMainStateKey)).toByteArray();
    if (!mainState.isEmpty()) {
        restoreState(mainState);
    }
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(
        QString::fromLatin1(WindowSessionDeckWindowsKey)).toByteArray());
    settings.endGroup();

    if (!document.isArray()) {
        return false;
    }

    QJsonArray restorableWindows;
    for (const QJsonValue& value : document.array()) {
        const QString filePath = value.toObject().value(QStringLiteral("path")).toString();
        if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
            restorableWindows.append(value);
        }
    }
    m_pendingDeckWindowSession = QJsonDocument(restorableWindows).toJson(QJsonDocument::Compact);
    return !restorableWindows.isEmpty();
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
        for (int reportIndex = 0; reportIndex < workspace->deck().reportCount(); ++reportIndex) {
            const ReportDefinition& report = workspace->deck().reportAt(reportIndex);
            QString type = tr("Report");
            if (report.formType == ReportFormType::Card) {
                type = tr("Card");
            } else if (report.formType == ReportFormType::Label) {
                type = tr("Label");
            }
            context.reports.append({type, report.name, reportIndex});
        }
        context.searchDirectionAvailable = workspace->hasLastSearchRequest();
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
    const bool hasDeck = activeDeckWorkspace() != nullptr;
    if (QAction* action = findUiAction(Command::ConfigureAddSecurity)) {
        const DeckWorkspace* workspace = activeDeckWorkspace();
        if (workspace == nullptr && templateDesigner != nullptr) {
            workspace = qobject_cast<DeckWorkspace*>(
                templateDesigner->property("ownerDeckWorkspace").value<QObject*>());
        }
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
    const int cardCount = workspace != nullptr ? workspace->deck().cardCount() : 0;
    const int currentCardIndex = workspace != nullptr ? workspace->currentCardIndex() : -1;
    const bool hasCards = cardCount > 0;
    const bool hasPreviousCard = hasCards && currentCardIndex > 0;
    const bool hasNextCard = hasCards && currentCardIndex + 1 < cardCount;
    if (QAction* action = findUiAction(Command::PhoneDial)) {
        action->setEnabled(true);
    }

    for (int commandId : {Command::CardDelete,
                          Command::CardDuplicate,
                          Command::SearchFind,
                          Command::SearchReplace}) {
        if (QAction* action = findUiAction(commandId)) {
            action->setEnabled(workspace != nullptr && hasCards);
        }
    }
    if (QAction* action = findUiAction(Command::SearchFindNext)) {
        action->setEnabled(workspace != nullptr && hasCards && workspace->hasLastSearchRequest());
    }
    for (int commandId : {Command::NavigateFirstCard,
                          Command::NavigatePreviousWindowful,
                          Command::NavigatePreviousCard}) {
        if (QAction* action = findUiAction(commandId)) {
            action->setEnabled(workspace != nullptr && hasPreviousCard);
        }
    }
    for (int commandId : {Command::NavigateLastCard,
                          Command::NavigateNextCard,
                          Command::NavigateNextWindowful}) {
        if (QAction* action = findUiAction(commandId)) {
            action->setEnabled(workspace != nullptr && hasNextCard);
        }
    }
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
        for (int commandId : {Command::EditCut, Command::EditCopy}) {
            if (QAction* action = findUiAction(commandId)) {
                action->setEnabled(designer->canCopyFrame());
            }
        }
        if (QAction* action = findUiAction(Command::EditPaste)) {
            action->setEnabled(designer->canPasteFrame());
        }
    }

    if (templateDesigner != nullptr) {
        const QList<int> templateDesignerCommandIds = {
            Command::FileSave,
            Command::FileSaveAs,
            Command::FileClose,
            Command::EditUndo,
            Command::EditCut,
            Command::EditCopy,
            Command::EditPaste,
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
            Command::ConfigureDeckDescription,
        };
        for (int commandId : templateDesignerCommandIds) {
            if (QAction* action = findUiAction(commandId)) {
                action->setEnabled(true);
            }
        }
        if (QAction* action = findUiAction(Command::EditClear)) {
            action->setEnabled(templateDesigner->selectedFrameIndex() >= 0);
        }
        for (int commandId : {Command::EditCut, Command::EditCopy}) {
            if (QAction* action = findUiAction(commandId)) {
                action->setEnabled(templateDesigner->canCopyFrame());
            }
        }
        if (QAction* action = findUiAction(Command::EditPaste)) {
            action->setEnabled(templateDesigner->canPasteFrame());
        }
    }

    if (QAction* action = findUiAction(Command::EditUndo)) {
        action->setEnabled(designer != nullptr
                ? designer->canUndo()
                : (templateDesigner != nullptr
                          ? templateDesigner->canUndo()
                          : (workspace != nullptr && workspace->canUndo())));
    }
    if (QAction* action = findUiAction(Command::CardUndelete)) {
        action->setEnabled(workspace != nullptr && workspace->canUndelete());
    }
    const bool tableNavigation = workspace != nullptr
        && workspace->viewMode() == DeckWorkspace::ViewMode::Table;
    const auto setNavigationAction = [this](int commandId, const QString& text, const QKeySequence& shortcut) {
        if (QAction* action = findUiAction(commandId)) {
            action->setText(text);
            action->setShortcut(shortcut);
        }
    };
    if (tableNavigation) {
        setNavigationAction(Command::NavigatePreviousCard, tr("&Previous Card"), QKeySequence(Qt::Key_Up));
        setNavigationAction(Command::NavigateNextCard, tr("&Next Card"), QKeySequence(Qt::Key_Down));
        setNavigationAction(Command::NavigatePreviousWindowful, tr("Pre&vious Windowful"), QKeySequence(Qt::Key_PageUp));
        setNavigationAction(Command::NavigateNextWindowful, tr("Ne&xt Windowful"), QKeySequence(Qt::Key_PageDown));
    } else {
        setNavigationAction(Command::NavigatePreviousCard, tr("&Previous Card"), QKeySequence(Qt::Key_PageUp));
        setNavigationAction(Command::NavigateNextCard, tr("&Next Card"), QKeySequence(Qt::Key_PageDown));
        setNavigationAction(Command::NavigatePreviousWindowful,
                            tr("Pre&vious Windowful"),
                            QKeySequence(Qt::CTRL | Qt::Key_PageUp));
        setNavigationAction(Command::NavigateNextWindowful,
                            tr("Ne&xt Windowful"),
                            QKeySequence(Qt::CTRL | Qt::Key_PageDown));
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
    subWindow->setWindowIcon(deckWindowIcon());
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
    subWindow->setWindowIcon(reportDesignerWindowIcon());
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
    connect(designer, &ReportDesignerWidget::selectedFrameChanged, this, [this]() {
        rebuildDesignerPropertyToolbar();
        updateCommandState();
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

    auto* designer = new TemplateDesignerWidget(
        workspace->deck().cardTemplateLayout(), workspace->deck().fields(), nullptr, workspace->deck().appearance());
    designer->setProperty("ownerDeckWorkspace", QVariant::fromValue<QObject*>(workspace));
    designer->setProperty("ownerDeckSubWindow", QVariant::fromValue<QObject*>(ownerSubWindow.data()));

    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(designer);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(templateDesignerWindowIcon());
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
    connect(designer, &TemplateDesignerWidget::commandRequested, this, [this](int commandId) {
        handleTemplateDesignerCommand(commandId);
        updateCommandState();
    });
    connect(designer, &TemplateDesignerWidget::selectedFieldChanged, this, [this]() {
        rebuildDesignerPropertyToolbar();
        updateCommandState();
    });
    connect(designer, &TemplateDesignerWidget::saveRequested, this, [this, designer](const CardTemplateLayout& layout) {
        auto* owner = qobject_cast<DeckWorkspace*>(designer->property("ownerDeckWorkspace").value<QObject*>());
        if (owner == nullptr) {
            QMessageBox::warning(this, tr("CardStack Templates"), tr("The source deck window is no longer available."));
            designer->markDirty();
            return;
        }

        QVector<int> sourceIndexes;
        sourceIndexes.reserve(designer->fieldDefinitions().size());
        for (int index = 0; index < designer->fieldDefinitions().size(); ++index) {
            sourceIndexes.append(index);
        }
        QString redefineError;
        owner->redefineFields(designer->fieldDefinitions(), sourceIndexes, &redefineError);
        if (!redefineError.isEmpty()) {
            QMessageBox::warning(this, tr("CardStack Templates"), redefineError);
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
    auto* designer = new TemplateDesignerWidget(
        draftDeck->cardTemplateLayout(), draftDeck->fields(), nullptr, draftDeck->appearance());
    designer->setProperty("draftTemplateDesigner", true);

    QMdiSubWindow* subWindow = m_mdiArea->addSubWindow(designer);
    subWindow->setAttribute(Qt::WA_DeleteOnClose);
    subWindow->setWindowIcon(templateDesignerWindowIcon());
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
    connect(designer, &TemplateDesignerWidget::commandRequested, this, [this](int commandId) {
        handleTemplateDesignerCommand(commandId);
        updateCommandState();
    });
    connect(designer, &TemplateDesignerWidget::selectedFieldChanged, this, [this]() {
        rebuildDesignerPropertyToolbar();
        updateCommandState();
    });
    connect(designer, &TemplateDesignerWidget::saveRequested, this, [this, designer, draftDeck](const CardTemplateLayout& layout) {
        draftDeck->setFields(designer->fieldDefinitions());
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
