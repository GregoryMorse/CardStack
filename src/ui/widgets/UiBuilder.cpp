#include "UiBuilder.h"

#include "Deck.h"
#include "UiIds.h"
#include "UiResourceData.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <tuple>

void qt_set_sequence_auto_mnemonic(bool enabled);

namespace CardStack {
namespace {

namespace Control = UiIds::Control;
namespace StringId = UiIds::StringId;

void enableLegacyMnemonics()
{
    static const bool enabled = [] {
        qt_set_sequence_auto_mnemonic(true);
        return true;
    }();
    Q_UNUSED(enabled);
}

QKeySequence legacyMenuShortcut(QString shortcutText)
{
    shortcutText = shortcutText.trimmed();
    shortcutText.replace(QStringLiteral("PageUp"), QStringLiteral("PgUp"), Qt::CaseInsensitive);
    shortcutText.replace(QStringLiteral("PageDn"), QStringLiteral("PgDown"), Qt::CaseInsensitive);
    if (shortcutText.compare(QStringLiteral("Del"), Qt::CaseInsensitive) == 0) {
        return QKeySequence(Qt::Key_Delete);
    }
    const QKeySequence shortcut = QKeySequence::fromString(shortcutText, QKeySequence::PortableText);
    Q_ASSERT_X(!shortcut.isEmpty(), "legacyMenuShortcut", qPrintable(QStringLiteral("Unrecognized shortcut: %1").arg(shortcutText)));
    return shortcut;
}

void bindDialogLabelMnemonics(QWidget* dialog)
{
    const QList<QWidget*> candidates = dialog->findChildren<QWidget*>();
    for (QLabel* label : dialog->findChildren<QLabel*>()) {
        if (label->buddy() != nullptr || QKeySequence::mnemonic(label->text()).isEmpty()) {
            continue;
        }

        const QRect labelRect(label->mapTo(dialog, QPoint(0, 0)), label->size());
        QWidget* bestCandidate = nullptr;
        int bestScore = std::numeric_limits<int>::max();
        for (QWidget* candidate : candidates) {
            if (candidate == label || candidate->focusPolicy() == Qt::NoFocus
                || qobject_cast<QAbstractButton*>(candidate) != nullptr) {
                continue;
            }

            const QRect candidateRect(candidate->mapTo(dialog, QPoint(0, 0)), candidate->size());
            const int verticalDistance = std::abs(candidateRect.center().y() - labelRect.center().y());
            const bool besideLabel = candidateRect.left() >= labelRect.right() - 2
                && verticalDistance <= std::max(labelRect.height(), candidateRect.height());
            const int horizontalOverlap = std::min(labelRect.right(), candidateRect.right())
                - std::max(labelRect.left(), candidateRect.left());
            const bool belowLabel = candidateRect.top() >= labelRect.bottom() - 2
                && horizontalOverlap > 0
                && candidateRect.top() - labelRect.bottom() <= labelRect.height();
            if (!besideLabel && !belowLabel) {
                continue;
            }

            const int score = besideLabel
                ? std::max(0, candidateRect.left() - labelRect.right()) + verticalDistance * 4
                : candidateRect.top() - labelRect.bottom()
                    + std::abs(candidateRect.center().x() - labelRect.center().x()) * 2;
            if (score < bestScore) {
                bestScore = score;
                bestCandidate = candidate;
            }
        }
        if (bestCandidate != nullptr) {
            label->setBuddy(bestCandidate);
        }
    }
}

constexpr int NarrowStaticDluWidth = 6;
constexpr int ComboMinimumContentsLength = 10;
constexpr int ComboDropDownArrowPaddingPx = 56;
constexpr int EditableComboMinimumWidthPx = 150;
constexpr int FixedComboMinimumWidthPx = 112;
constexpr int EditableComboMaximumWidthPx = 280;
constexpr int FixedComboMaximumWidthPx = 190;
constexpr int NewFileSourceComboWidthPx = 310;
constexpr int NewFileSourcePopupWidthPx = 460;
constexpr int ShortNumericEditWidthPx = 48;
constexpr int MicroScrollWidthPx = 10;
constexpr int DialogControlGapPx = 8;
constexpr int DialogOuterMarginPx = 16;
constexpr int GroupBoxInnerMarginPx = 8;
constexpr int GroupBoxVerticalGapPx = 14;
constexpr int ComboMinimumShrunkWidthPx = 92;
constexpr int TextMinimumShrunkWidthPx = 80;
constexpr int HtmlDialogWidthPx = 760;
constexpr int HtmlDialogHeightPx = 620;
constexpr int MicroScrollSnapTolerancePx = 96;

namespace WinButtonStyle {
constexpr quint32 Mask = 0x000fU;
constexpr int CheckBox = 0x02;
constexpr int AutoCheckBox = 0x03;
constexpr int RadioButton = 0x04;
constexpr int GroupBox = 0x07;
constexpr int AutoRadioButton = 0x09;
} // namespace WinButtonStyle

namespace WinComboBoxStyle {
constexpr quint32 TypeMask = 0x0003U;
constexpr int Simple = 0x01;
constexpr int DropDown = 0x02;
constexpr int DropDownList = 0x03;
} // namespace WinComboBoxStyle

namespace WinStaticStyle {
constexpr quint32 TypeMask = 0x001fU;
constexpr int Icon = 0x03;
} // namespace WinStaticStyle

QPixmap legacyStaticIcon(const QString& resourceName, const QSize& requestedSize)
{
    const QSize size(std::max(20, requestedSize.width()), std::max(20, requestedSize.height()));
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    if (resourceName.compare(QStringLiteral("IDPHONE"), Qt::CaseInsensitive) != 0) {
        return pixmap;
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = std::min(size.width(), size.height()) / 24.0;
    painter.scale(scale, scale);
    const QColor outline(35, 82, 91);
    const QColor body(221, 239, 236);
    painter.setPen(QPen(outline, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(body);
    painter.drawRoundedRect(QRectF(4.0, 9.0, 16.0, 11.0), 2.2, 2.2);
    painter.setBrush(QColor(248, 252, 250));
    painter.drawEllipse(QRectF(8.0, 11.0, 8.0, 7.0));
    painter.setBrush(outline);
    painter.drawEllipse(QRectF(10.9, 13.4, 2.2, 2.2));
    painter.setBrush(body);
    painter.drawRoundedRect(QRectF(2.5, 4.0, 19.0, 5.8), 2.5, 2.5);
    painter.drawLine(QPointF(6.0, 9.5), QPointF(6.0, 12.0));
    painter.drawLine(QPointF(18.0, 9.5), QPointF(18.0, 12.0));
    return pixmap;
}

QString modernizedVisibleText(QString text)
{
    const QString legacyProductName = QStringLiteral("Button") + QStringLiteral("File");
    text.replace(legacyProductName, QStringLiteral("CardStack"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("LegacyDeck"), QStringLiteral("CardStack"), Qt::CaseInsensitive);
    return text;
}

QString mnemonicText(const QString& text)
{
    return modernizedVisibleText(text);
}

QString plainVisibleText(QString text)
{
    text = modernizedVisibleText(text);
    QString result;
    result.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) != QLatin1Char('&')) {
            result.append(text.at(index));
            continue;
        }

        if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('&')) {
            result.append(QLatin1Char('&'));
            ++index;
        }
    }
    return result;
}

void showCardStackHelp(QWidget* parent)
{
    QDialog dialog(parent);
    const QString dialogName = parent == nullptr ? QString() : parent->property("legacyDialogName").toString();
    dialog.setWindowTitle(dialogName.isEmpty() ? QObject::tr("CardStack Help") : QObject::tr("%1 Help").arg(dialogName));
    dialog.resize(HtmlDialogWidthPx, HtmlDialogHeightPx);

    auto* layout = new QVBoxLayout(&dialog);
    auto* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);
    QString dialogHelpHtml;
    if (dialogName == QStringLiteral("SEARCH")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Find</h1>"
            "<p>Find searches the current deck without changing card data.</p>"
            "<ul>"
            "<li><b>Search for</b> accepts typed text or a recent search.</li>"
            "<li><b>Search in data box</b> limits the match to one field, or searches every data box.</li>"
            "<li><b>Search type</b> controls contains, begins-with, comparison, and negative matches.</li>"
            "<li><b>Whole word</b>, <b>Case sensitive</b>, and <b>Sounds like</b> refine matching.</li>"
            "<li>The second row can combine criteria with And or Or.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("REPLACE")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Replace</h1>"
            "<p>Replace finds matching data and updates the current match or every match in the deck.</p>"
            "<ul>"
            "<li>The search controls work the same way as the Find dialog.</li>"
            "<li><b>Replace with</b> is constrained by the target data box length.</li>"
            "<li><b>Replace All</b> updates all matching cards and can be undone as a deck edit.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("NEWFILE")) {
        dialogHelpHtml = QObject::tr(
            "<h1>New Deck</h1>"
            "<p>Create a ready-to-use deck or open the visual template designer immediately.</p>"
            "<ul>"
            "<li><b>New deck from template</b> creates a normal deck from the selected template.</li>"
            "<li><b>Design deck from scratch</b> creates a minimal deck and opens the template designer.</li>"
            "<li><b>Design deck patterned after template</b> creates the selected template deck and opens the designer.</li>"
            "<li><b>Design deck patterned after deck</b> clones the active deck layout for editing.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("CHOOSECOLOR")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Colors</h1>"
            "<p>Choose a color role, then select a swatch. System palette mode follows the desktop theme colors.</p>");
    } else if (dialogName == QStringLiteral("SORT")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Change Deck Index</h1>"
            "<p>Choose up to three data boxes for the table/index order. Reverse switches that level to descending order.</p>");
    } else if (dialogName == QStringLiteral("MERGEDLG")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Merge Mapping</h1>"
            "<p>Merge copies cards from another deck into the active deck.</p>"
            "<ul>"
            "<li>Choose whether to merge all source cards or only selected source cards.</li>"
            "<li>Map each source data box to the destination data box that should receive it.</li>"
            "<li>Unmapped destination boxes remain empty for merged cards.</li>"
            "<li>The merge is a deck edit and can be undone before saving.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("EXPORT")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Export</h1>"
            "<p>Export writes cards from the active deck to an interchange file.</p>"
            "<ul>"
            "<li>Choose all cards or the current selected-card set.</li>"
            "<li>Move fields into the destination list to choose export order.</li>"
            "<li>Use CSV or tab-separated text for modern spreadsheet/database exchange.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("PRINT")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Print</h1>"
            "<p>Print uses the selected report design for the current deck.</p>"
            "<ul>"
            "<li>Choose the current card, all cards, or selected cards.</li>"
            "<li>Print Preview renders the same report frames before sending output to the printer.</li>"
            "<li>Printer Setup opens the platform printer/page setup where available.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("CALL") || dialogName == QStringLiteral("PHNDEF")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Phone Dialing</h1>"
            "<p>CardStack opens calls through the operating system phone-link handler.</p>"
            "<ul>"
            "<li>Select or type a phone number.</li>"
            "<li>The number is copied to the clipboard.</li>"
            "<li>If the system has a phone app registered for tel: links, CardStack asks it to place the call.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("IMPEXAMINE")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Import Examine</h1>"
            "<p>Use Import Examine to review field names and record structure before importing legacy or interchange data.</p>"
            "<ul>"
            "<li>Confirm that each source column maps to the intended CardStack data box.</li>"
            "<li>Review detected notes/memo fields before completing the import.</li>"
            "<li>If the source is malformed, cancel and repair the source file before importing.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("DESIGNREPORTS")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Available Reports</h1>"
            "<p>This dialog manages report designs stored with the active deck.</p>"
            "<ul>"
            "<li>New creates a report design for the deck.</li>"
            "<li>Modify opens the selected report in the visual report designer.</li>"
            "<li>Delete removes the selected report from the deck after confirmation.</li>"
            "<li>Add Defaults restores standard card/list report designs when they are missing.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("SAVEDESIGN")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Save Report Design</h1>"
            "<p>Name the current report design and save it into the active deck.</p>"
            "<ul>"
            "<li>Select an existing report name to replace it.</li>"
            "<li>Type a new name to save a new report design.</li>"
            "<li>Report packages can also be exported for sharing.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("REPORTFORM")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Report Form</h1>"
            "<p>Report form settings define the printable page or label grid used by the report renderer.</p>"
            "<ul>"
            "<li>Choose card, label, report, or custom form type.</li>"
            "<li>Set page size, margins, rows, columns, gutters, and orientation.</li>"
            "<li>The preview updates from these settings when the report is rendered.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("ADDSYSTEMBOX")) {
        dialogHelpHtml = QObject::tr(
            "<h1>Add System Box</h1>"
            "<p>System boxes insert generated values into report designs.</p>"
            "<ul>"
            "<li>Date/time boxes render the current date or time using the chosen format.</li>"
            "<li>Number boxes render values such as page number or card count.</li>"
            "<li>System fields render deck/report metadata.</li>"
            "<li>Alignment and text style controls affect the generated text frame.</li>"
            "</ul>");
    } else if (dialogName == QStringLiteral("GETUSERNAME")) {
        dialogHelpHtml = QObject::tr(
            "<h1>User Name</h1>"
            "<p>Enter the display name that should be stored in deck metadata or used by workflows that ask for an operator name.</p>");
    }

    QFile helpFile(QStringLiteral(":/cardstack/help/index.html"));
    if (!dialogHelpHtml.isEmpty()) {
        browser->setHtml(dialogHelpHtml);
    } else if (helpFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        browser->setHtml(QString::fromUtf8(helpFile.readAll()));
    } else {
        browser->setHtml(QObject::tr("<h1>CardStack Help</h1><p>Help content could not be loaded.</p>"));
    }
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

QString uiText(const char* text)
{
    return QString::fromUtf8(text == nullptr ? "" : text);
}

const UiResourceData::UiMenu* findMenu(int menuId)
{
    return UiResourceData::findMenu(menuId);
}

const UiResourceData::UiDialog* findDialog(const QString& name)
{
    return UiResourceData::findDialog(name.toUtf8().constData());
}

QString legacyString(int stringId)
{
    if (const UiResourceData::UiString* stringValue = UiResourceData::findString(stringId)) {
        return uiText(stringValue->text);
    }
    return {};
}

QString displayTextAndShortcut(const QString& rawText, QKeySequence* shortcut)
{
    const QStringList parts = rawText.split(QLatin1Char('\t'));
    if (shortcut != nullptr && parts.size() > 1) {
        const QString shortcutText = parts.last().trimmed();
        if (!shortcutText.isEmpty()) {
            *shortcut = legacyMenuShortcut(shortcutText);
        }
    }

    return modernizedVisibleText(parts.first());
}

void populateMenu(
    QMenu* menu,
    const UiResourceData::UiMenuItem* items,
    std::size_t itemCount,
    QObject* actionParent,
    const std::function<void(QAction*)>& configureAction)
{
    for (std::size_t index = 0; index < itemCount; ++index) {
        const UiResourceData::UiMenuItem& item = items[index];
        if (item.separator) {
            menu->addSeparator();
            continue;
        }

        QKeySequence shortcut;
        const QString text = displayTextAndShortcut(uiText(item.text), &shortcut);
        if (item.children != nullptr && item.childCount > 0) {
            QMenu* childMenu = menu->addMenu(text);
            populateMenu(childMenu, item.children, item.childCount, actionParent, configureAction);
            continue;
        }

        QAction* action = new QAction(text, actionParent);
        action->setData(item.id);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        if (configureAction) {
            configureAction(action);
        }
        menu->addAction(action);
    }
}

class ColorSwatchGrid : public QWidget {
public:
    explicit ColorSwatchGrid(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_customColors({
              QColor(0, 0, 0),
              QColor(0, 0, 128),
              QColor(128, 0, 0),
              QColor(128, 0, 128),
              QColor(255, 255, 255),
              QColor(192, 192, 192),
              QColor(255, 255, 224),
          })
    {
        setFocusPolicy(Qt::StrongFocus);
        setAutoFillBackground(true);
        setProperty("paletteColorCount", 48);
        setProperty("hasDeckPreview", true);
    }

    void setSelectedRole(int role)
    {
        m_selectedRole = std::clamp(role, 0, static_cast<int>(m_customColors.size()) - 1);
        update();
    }

    void setUseSystemColors(bool useSystemColors)
    {
        m_useSystemColors = useSystemColors;
        update();
    }

    void setCustomColors(const QStringList& colors)
    {
        for (int index = 0; index < m_customColors.size() && index < colors.size(); ++index) {
            const QColor color(colors.at(index));
            if (color.isValid()) {
                m_customColors[index] = color;
            }
        }
        update();
    }

    QStringList customColors() const
    {
        QStringList colors;
        for (const QColor& color : m_customColors) {
            colors.append(color.name(QColor::HexRgb));
        }
        return colors;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().shadow().color());
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRect paletteRect = rect().adjusted(8, 8, -8, -72);
        const int swatchWidth = std::max(1, paletteRect.width() / 8);
        const int swatchHeight = std::max(1, paletteRect.height() / 6);
        const QVector<QColor>& colors = paletteColors();
        for (int index = 0; index < colors.size(); ++index) {
            const QRect swatch(
                paletteRect.left() + (index % 8) * swatchWidth,
                paletteRect.top() + (index / 8) * swatchHeight,
                swatchWidth,
                swatchHeight);
            painter.fillRect(swatch.adjusted(1, 1, -1, -1), colors.at(index));
            painter.setPen(colors.at(index) == colorForRole(m_selectedRole)
                               ? Qt::black
                               : QColor(QStringLiteral("#707070")));
            painter.drawRect(swatch.adjusted(0, 0, -1, -1));
            if (colors.at(index) == colorForRole(m_selectedRole)) {
                painter.drawRect(swatch.adjusted(2, 2, -3, -3));
            }
        }

        const QRect preview = rect().adjusted(8, height() - 64, -8, -8);
        painter.fillRect(preview, colorForRole(static_cast<int>(DeckColorRole::CardBackground)));
        painter.setPen(QColor(QStringLiteral("#707070")));
        painter.drawRect(preview.adjusted(0, 0, -1, -1));

        const QRect indexPreview(preview.left() + 5, preview.top() + 4, preview.width() - 10, 15);
        painter.fillRect(indexPreview, colorForRole(static_cast<int>(DeckColorRole::IndexBackground)));
        painter.setPen(colorForRole(static_cast<int>(DeckColorRole::IndexForeground)));
        painter.drawText(indexPreview.adjusted(4, 0, -4, 0), Qt::AlignVCenter, tr("Index"));

        painter.setPen(colorForRole(static_cast<int>(DeckColorRole::NameForeground)));
        painter.drawText(QRect(preview.left() + 6, preview.top() + 24, 52, 20),
                         Qt::AlignRight | Qt::AlignVCenter,
                         tr("Name"));
        const QRect dataPreview(preview.left() + 64, preview.top() + 24,
                                std::max(20, preview.width() - 70), 20);
        painter.fillRect(dataPreview, colorForRole(static_cast<int>(DeckColorRole::DataBackground)));
        painter.setPen(colorForRole(static_cast<int>(DeckColorRole::DataForeground)));
        painter.drawText(dataPreview.adjusted(4, 0, -4, 0), Qt::AlignVCenter, tr("Sample data"));
        painter.setPen(colorForRole(static_cast<int>(DeckColorRole::TextForeground)));
        painter.drawText(QRect(preview.left() + 6, preview.bottom() - 14,
                               preview.width() - 12, 12),
                         Qt::AlignVCenter,
                         tr("Text"));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const int paletteIndex = paletteIndexAt(event->position().toPoint());
        if (paletteIndex < 0) {
            return;
        }

        if (m_useSystemColors) {
            return;
        }
        m_customColors[m_selectedRole] = paletteColors().at(paletteIndex);
        update();
    }

private:
    QColor colorForRole(int role) const
    {
        if (!m_useSystemColors) {
            return m_customColors.at(role);
        }

        switch (static_cast<DeckColorRole>(role)) {
        case DeckColorRole::IndexBackground:
            return QColor(QStringLiteral("#c0c0c0"));
        case DeckColorRole::DataBackground:
        case DeckColorRole::CardBackground:
            return QColor(QStringLiteral("#ffffff"));
        default:
            return QColor(QStringLiteral("#000000"));
        }
    }

    int paletteIndexAt(const QPoint& point) const
    {
        const QRect paletteRect = rect().adjusted(8, 8, -8, -72);
        if (!paletteRect.contains(point)) {
            return -1;
        }
        const int swatchWidth = std::max(1, paletteRect.width() / 8);
        const int swatchHeight = std::max(1, paletteRect.height() / 6);
        const int column = std::clamp((point.x() - paletteRect.left()) / swatchWidth, 0, 7);
        const int row = std::clamp((point.y() - paletteRect.top()) / swatchHeight, 0, 5);
        return row * 8 + column;
    }

    static const QVector<QColor>& paletteColors()
    {
        static const QVector<QColor> colors = [] {
            static const char* const values[] = {
                "#ff8080", "#ffffe8", "#80ff80", "#00ff80", "#80ffff", "#0080ff", "#ff80c0", "#ff80ff",
                "#ff0000", "#ffff80", "#80ff00", "#00ff40", "#00ffff", "#0080c0", "#8080c0", "#ff00ff",
                "#804040", "#ffff00", "#00ff00", "#008080", "#004080", "#8080ff", "#800040", "#ff0080",
                "#800000", "#ff8000", "#008000", "#008040", "#0000ff", "#0000a0", "#800080", "#8000ff",
                "#400000", "#804000", "#004000", "#004040", "#000080", "#000040", "#400040", "#400080",
                "#000000", "#808000", "#808040", "#808080", "#408080", "#c0c0c0", "#828282", "#ffffff",
            };
            QVector<QColor> result;
            result.reserve(48);
            for (const char* value : values) {
                result.append(QColor(QString::fromLatin1(value)));
            }
            return result;
        }();
        return colors;
    }

    QVector<QColor> m_customColors;
    int m_selectedRole = 5;
    bool m_useSystemColors = false;
};

class PicturePreview : public QWidget {
public:
    explicit PicturePreview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(true);
    }

    void setSampleText(const QString& sampleText)
    {
        m_sampleText = sampleText;
        update();
    }

    void setSampleAlignment(Qt::Alignment alignment)
    {
        m_alignment = alignment;
        update();
    }

    void setSampleStyle(bool bold, bool italic, bool underline)
    {
        m_bold = bold;
        m_italic = italic;
        m_underline = underline;
        update();
    }

    void setPixmap(const QPixmap& pixmap)
    {
        m_pixmap = pixmap;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().shadow().color());
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        if (!m_pixmap.isNull()) {
            const QRect target = rect().adjusted(8, 6, -8, -6);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawPixmap(target, m_pixmap.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }

        if (property("formSample").toBool()) {
            const int rows = std::max(1, property("formSampleRows").toInt());
            const int columns = std::max(1, property("formSampleColumns").toInt());
            const int widthMils = std::max(1, property("formSampleWidthMils").toInt());
            const int heightMils = std::max(1, property("formSampleHeightMils").toInt());
            const QRectF canvasRect = rect().adjusted(12, 10, -12, -10);
            const qreal pageAspect = static_cast<qreal>(widthMils) / static_cast<qreal>(heightMils);
            QRectF pageRect = canvasRect;
            if (pageAspect > 0.0 && canvasRect.height() > 0.0) {
                const qreal canvasAspect = canvasRect.width() / canvasRect.height();
                if (pageAspect > canvasAspect) {
                    const qreal pageHeight = canvasRect.width() / pageAspect;
                    pageRect.setTop(canvasRect.center().y() - pageHeight / 2.0);
                    pageRect.setBottom(canvasRect.center().y() + pageHeight / 2.0);
                } else {
                    const qreal pageWidth = canvasRect.height() * pageAspect;
                    pageRect.setLeft(canvasRect.center().x() - pageWidth / 2.0);
                    pageRect.setRight(canvasRect.center().x() + pageWidth / 2.0);
                }
            }

            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
            painter.setBrush(QColor(255, 255, 255));
            painter.drawRect(pageRect);

            for (int column = 1; column < columns; ++column) {
                const qreal x = pageRect.left() + pageRect.width() * column / columns;
                painter.drawLine(QPointF(x, pageRect.top()), QPointF(x, pageRect.bottom()));
            }
            for (int row = 1; row < rows; ++row) {
                const qreal y = pageRect.top() + pageRect.height() * row / rows;
                painter.drawLine(QPointF(pageRect.left(), y), QPointF(pageRect.right(), y));
            }
            return;
        }

        if (property("lineFrameSample").toBool()) {
            const int shape = property("lineFrameShape").toInt();
            const int lineStyle = property("lineFrameLineStyle").toInt();
            const int fillPattern = property("lineFrameFillPattern").toInt();
            const int cornerRadius = property("lineFrameCornerRadius").toInt();
            static constexpr Qt::PenStyle PenStyles[] = {
                Qt::SolidLine,
                Qt::DashLine,
                Qt::DotLine,
                Qt::DashDotLine,
                Qt::DashDotDotLine,
            };
            static constexpr Qt::BrushStyle FillStyles[] = {
                Qt::NoBrush,
                Qt::Dense7Pattern,
                Qt::Dense6Pattern,
                Qt::Dense5Pattern,
                Qt::Dense4Pattern,
                Qt::Dense3Pattern,
                Qt::Dense2Pattern,
                Qt::Dense1Pattern,
            };
            const QRectF sampleRect = rect().adjusted(16, 14, -16, -14);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(
                palette().text().color(),
                2,
                PenStyles[std::clamp(lineStyle, 0, 4)]));
            painter.setBrush(QBrush(
                palette().base().color(),
                FillStyles[std::clamp(fillPattern, 0, 7)]));
            if (shape == 1) {
                painter.drawLine(sampleRect.left(), sampleRect.center().y(), sampleRect.right(), sampleRect.center().y());
            } else if (shape == 2) {
                painter.drawLine(sampleRect.center().x(), sampleRect.top(), sampleRect.center().x(), sampleRect.bottom());
            } else {
                const qreal radius = std::min<qreal>(
                    std::clamp(cornerRadius, 0, 200) / 4.0,
                    std::min(sampleRect.width(), sampleRect.height()) / 2.0);
                painter.drawRoundedRect(sampleRect, radius, radius);
            }
            return;
        }

        QFont sampleFont = font();
        sampleFont.setBold(m_bold);
        sampleFont.setItalic(m_italic);
        sampleFont.setUnderline(m_underline);
        painter.setFont(sampleFont);
        painter.setPen(palette().text().color());
        painter.drawText(rect().adjusted(5, 1, -5, -1), m_alignment | Qt::AlignVCenter, m_sampleText);
    }

private:
    QString m_sampleText;
    QPixmap m_pixmap;
    Qt::Alignment m_alignment = Qt::AlignLeft;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
};

class MicroScrollSpinBox final : public QSpinBox {
public:
    explicit MicroScrollSpinBox(QWidget* parent = nullptr)
        : QSpinBox(parent)
    {
        setAccelerated(true);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setFocusPolicy(Qt::NoFocus);
        setFrame(false);
        setRange(-9999, 9999);
        setValue(0);
        setCursor(Qt::ArrowCursor);
        if (QLineEdit* editor = lineEdit()) {
            editor->hide();
            editor->setReadOnly(true);
        }
    }

    QSize sizeHint() const override
    {
        return {10, 14};
    }

    QSize minimumSizeHint() const override
    {
        return {8, 12};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QRect outer = rect().adjusted(0, 0, -1, -1);
        const int splitY = height() / 2;
        const QRect upRect(0, 0, width(), splitY);
        const QRect downRect(0, splitY, width(), height() - splitY);

        painter.fillRect(rect(), palette().button());
        painter.setPen(palette().mid().color());
        painter.drawRect(outer);
        painter.drawLine(1, splitY, std::max(1, width() - 2), splitY);

        painter.setPen(QPen(palette().buttonText().color(), 1));
        drawArrow(&painter, upRect, true);
        drawArrow(&painter, downRect, false);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            stepBy(event->pos().y() < height() / 2 ? 1 : -1);
            event->accept();
            return;
        }
        QSpinBox::mousePressEvent(event);
    }

private:
    static void drawArrow(QPainter* painter, const QRect& rect, bool up)
    {
        const int centerX = rect.center().x();
        const int centerY = rect.center().y();
        if (up) {
            painter->drawLine(centerX - 2, centerY + 1, centerX, centerY - 2);
            painter->drawLine(centerX, centerY - 2, centerX + 2, centerY + 1);
        } else {
            painter->drawLine(centerX - 2, centerY - 1, centerX, centerY + 2);
            painter->drawLine(centerX, centerY + 2, centerX + 2, centerY - 1);
        }
    }
};

QWidget* makePlaceholder(QWidget* parent, const QString& className, const QString& text)
{
    if (className == QStringLiteral("TN_COLORSEL")) {
        return new ColorSwatchGrid(parent);
    }

    if (className == QStringLiteral("MicroScroll")) {
        return new MicroScrollSpinBox(parent);
    }

    if (className == QStringLiteral("scrollbar")) {
        return new QScrollBar(Qt::Vertical, parent);
    }

    if (className.compare(QStringLiteral("BTN_pict"), Qt::CaseInsensitive) == 0 ||
        className.compare(QStringLiteral("ALIGNED_pict"), Qt::CaseInsensitive) == 0) {
        return new PicturePreview(parent);
    }

    if (className == QStringLiteral("bitmapbutton")) {
        auto* button = new QToolButton(parent);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return button;
    }

    auto* button = new QPushButton(mnemonicText(text), parent);
    if (text.isEmpty()) {
        button->setText(className);
    }
    return button;
}

QWidget* createControl(QWidget* parent, const UiResourceData::UiControl& control)
{
    const QString className = uiText(control.className);
    const QString text = uiText(control.text);
    const int id = control.id;
    const quint32 style = control.style;

    QWidget* widget = nullptr;
    if (className == QStringLiteral("static")) {
        const int staticType = static_cast<int>(style & WinStaticStyle::TypeMask);
        if (staticType == WinStaticStyle::Icon) {
            auto* label = new QLabel(parent);
            label->setAlignment(Qt::AlignCenter);
            label->setPixmap(legacyStaticIcon(text, QSize(control.width, control.height)));
            label->setAccessibleName(text.compare(QStringLiteral("IDPHONE"), Qt::CaseInsensitive) == 0
                    ? QObject::tr("Phone")
                    : QObject::tr("Icon"));
            label->setProperty("legacyIconResource", text);
            widget = label;
        } else if (text.isEmpty() && control.width <= NarrowStaticDluWidth) {
            auto* frame = new QFrame(parent);
            frame->setFrameShape(QFrame::VLine);
            frame->setFrameShadow(QFrame::Sunken);
            widget = frame;
        } else {
            auto* label = new QLabel(plainVisibleText(text), parent);
            label->setWordWrap(false);
            widget = label;
        }
    } else if (className == QStringLiteral("edit")) {
        widget = new QLineEdit(parent);
    } else if (className == QStringLiteral("listbox")) {
        widget = new QListWidget(parent);
    } else if (className == QStringLiteral("combobox")) {
        auto* comboBox = new QComboBox(parent);
        const int comboType = static_cast<int>(style & WinComboBoxStyle::TypeMask);
        comboBox->setEditable(comboType != WinComboBoxStyle::DropDownList);
        comboBox->setMinimumContentsLength(ComboMinimumContentsLength);
        comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        widget = comboBox;
    } else if (className == QStringLiteral("button")) {
        const int buttonStyle = static_cast<int>(style & WinButtonStyle::Mask);
        if (buttonStyle == WinButtonStyle::GroupBox) {
            widget = new QGroupBox(plainVisibleText(text), parent);
        } else if (buttonStyle == WinButtonStyle::CheckBox || buttonStyle == WinButtonStyle::AutoCheckBox) {
            widget = new QCheckBox(mnemonicText(text), parent);
        } else if (buttonStyle == WinButtonStyle::RadioButton || buttonStyle == WinButtonStyle::AutoRadioButton) {
            widget = new QRadioButton(mnemonicText(text), parent);
        } else {
            widget = new QPushButton(mnemonicText(text), parent);
        }
    } else {
        widget = makePlaceholder(parent, className, plainVisibleText(text));
    }

    widget->setProperty("originalControlId", id);
    widget->setProperty("uiControlClass", className);
    widget->setObjectName(QStringLiteral("ui_%1_%2").arg(id).arg(className));
#ifndef NDEBUG
    widget->setToolTip(QStringLiteral("%1: %2").arg(id).arg(className));
#endif
    return widget;
}

QRect dialogUnitsToPixels(const QWidget* widget, int x, int y, int width, int height)
{
    const QFontMetrics metrics(widget->font());
    const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    const int averageWidth = std::max(1, metrics.horizontalAdvance(alphabet) / static_cast<int>(alphabet.size()));
    const int averageHeight = std::max(1, metrics.height());

    const auto scaleX = [averageWidth](int value) { return value * averageWidth / 4; };
    const auto scaleY = [averageHeight](int value) { return value * averageHeight / 8; };

    return QRect(
        scaleX(x),
        scaleY(y),
        std::max(1, scaleX(width)),
        std::max(1, scaleY(height)));
}

QRect normalizedControlGeometry(QWidget* widget, const UiResourceData::UiControl& control)
{
    QRect rect = dialogUnitsToPixels(
        widget->parentWidget() == nullptr ? widget : widget->parentWidget(),
        control.x,
        control.y,
        control.width,
        control.height);
    const QString className = widget->property("uiControlClass").toString();

    if (className == QStringLiteral("combobox") || className == QStringLiteral("edit")) {
        rect.setHeight(std::max(rect.height(), widget->sizeHint().height()));
        rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
    } else if (qobject_cast<QAbstractButton*>(widget) != nullptr) {
        rect.setHeight(std::max(rect.height(), widget->sizeHint().height()));
        rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
    } else if (qobject_cast<QLabel*>(widget) != nullptr) {
        rect.setHeight(std::max(rect.height(), widget->sizeHint().height()));
        rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
    }

    if (className == QStringLiteral("combobox")) {
        rect.setHeight(widget->sizeHint().height());
    }

    return rect;
}

bool isTextSizedControl(const QWidget* widget)
{
    return qobject_cast<const QLabel*>(widget) != nullptr ||
        qobject_cast<const QAbstractButton*>(widget) != nullptr ||
        qobject_cast<const QComboBox*>(widget) != nullptr ||
        qobject_cast<const QLineEdit*>(widget) != nullptr;
}

bool rowsOverlap(const QRect& first, const QRect& second)
{
    const int top = std::max(first.top(), second.top());
    const int bottom = std::min(first.bottom(), second.bottom());
    return bottom >= top && (bottom - top + 1) >= std::min(first.height(), second.height()) / 2;
}

int preferredTextControlWidth(const QWidget* widget)
{
    int preferredWidth = widget->sizeHint().width();
    if (const auto* comboBox = qobject_cast<const QComboBox*>(widget)) {
        QFontMetrics metrics(comboBox->font());
        for (int index = 0; index < comboBox->count(); ++index) {
            preferredWidth = std::max(
                preferredWidth,
                metrics.horizontalAdvance(comboBox->itemText(index)) + ComboDropDownArrowPaddingPx);
        }
        preferredWidth = std::max(
            preferredWidth,
            comboBox->isEditable() ? EditableComboMinimumWidthPx : FixedComboMinimumWidthPx);
        preferredWidth = std::min(
            preferredWidth,
            comboBox->isEditable() ? EditableComboMaximumWidthPx : FixedComboMaximumWidthPx);
    } else if (qobject_cast<const QLineEdit*>(widget) != nullptr) {
        const QString dialogName = widget->window()->property("legacyDialogName").toString();
        const int controlId = widget->property("originalControlId").toInt();
        if ((dialogName == QStringLiteral("PRINT") && controlId == Control::PrintCopyCount) ||
            controlId == Control::SecurityPassword ||
            (dialogName == QStringLiteral("PHNDEF") &&
             (controlId == Control::PhoneLongDistancePrefix ||
              controlId == Control::PhoneOutsideLinePrefix ||
              controlId == Control::PhoneLocalAreaCode))) {
            return controlId == Control::SecurityPassword ? 92 : ShortNumericEditWidthPx;
        }
        preferredWidth = std::max(preferredWidth, EditableComboMinimumWidthPx);
    } else if (const auto* label = qobject_cast<const QLabel*>(widget)) {
        const QString text = plainVisibleText(label->text());
        if (!text.trimmed().isEmpty()) {
            const QFontMetrics metrics(label->font());
            preferredWidth = metrics.horizontalAdvance(text) + 4;
        }
    }
    return preferredWidth;
}

QWidget* directControlById(QDialog* dialog, int controlId)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        if (widget->property("originalControlId").toInt() == controlId) {
            return widget;
        }
    }
    return nullptr;
}

void setDirectControlWidth(QDialog* dialog, int controlId, int width)
{
    QWidget* widget = directControlById(dialog, controlId);
    if (widget == nullptr) {
        return;
    }

    QRect rect = widget->geometry();
    rect.setWidth(width);
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
    widget->setGeometry(rect);
}

QGroupBox* directGroupBoxByTitle(QDialog* dialog, const QString& title)
{
    const QList<QGroupBox*> groups = dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
    for (QGroupBox* group : groups) {
        if (plainVisibleText(group->title()) == title) {
            return group;
        }
    }
    return nullptr;
}

void setControlGeometry(QDialog* dialog, int controlId, const QRect& rect)
{
    QWidget* widget = directControlById(dialog, controlId);
    if (widget == nullptr) {
        return;
    }
    widget->setGeometry(rect);
}

void normalizeMicroScrollAdjacency(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        if (widget->isHidden() ||
            widget->property("uiControlClass").toString() != QStringLiteral("MicroScroll")) {
            continue;
        }

        const QRect microRect = widget->geometry();
        QWidget* nearestEdit = nullptr;
        int nearestDistance = 1000000;
        for (QWidget* candidate : controls) {
            auto* edit = qobject_cast<QLineEdit*>(candidate);
            if (edit == nullptr || edit->isHidden()) {
                continue;
            }

            const QRect editRect = edit->geometry();
            const int verticalDistance = std::abs(editRect.center().y() - microRect.center().y());
            if (verticalDistance > std::max(editRect.height(), microRect.height())) {
                continue;
            }

            const int targetLeft = editRect.right() + 1;
            const int horizontalDistance = std::abs(targetLeft - microRect.left());
            if (horizontalDistance > MicroScrollSnapTolerancePx) {
                continue;
            }

            const int distance = horizontalDistance + verticalDistance;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestEdit = edit;
            }
        }

        if (nearestEdit == nullptr) {
            continue;
        }

        const QRect editRect = nearestEdit->geometry();
        QRect snappedRect(editRect.right() + 1, editRect.top(), MicroScrollWidthPx, editRect.height());
        widget->setMinimumWidth(MicroScrollWidthPx);
        widget->setMaximumWidth(MicroScrollWidthPx);
        widget->setGeometry(snappedRect);
    }
}

void expandGroupBoxesToContainChildren(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        auto* groupBox = qobject_cast<QGroupBox*>(widget);
        if (groupBox == nullptr || groupBox->isHidden()) {
            continue;
        }

        QRect groupRect = groupBox->geometry();
        QRect expandedRect = groupRect;
        const QString title = plainVisibleText(groupBox->title());
        if (!title.trimmed().isEmpty()) {
            const QFontMetrics metrics(groupBox->font());
            expandedRect.setRight(std::max(
                expandedRect.right(),
                groupRect.left() + metrics.horizontalAdvance(title) + GroupBoxInnerMarginPx * 4));
        }
        for (QWidget* child : controls) {
            if (child == widget ||
                child->isHidden() ||
                qobject_cast<QGroupBox*>(child) != nullptr) {
                continue;
            }

            const QRect childRect = child->geometry();
            if (!groupRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                    .contains(childRect.center())) {
                continue;
            }

            expandedRect.setRight(std::max(expandedRect.right(), childRect.right() + GroupBoxInnerMarginPx));
            expandedRect.setBottom(std::max(expandedRect.bottom(), childRect.bottom() + GroupBoxInnerMarginPx));
        }

        if (expandedRect != groupRect) {
            groupBox->setGeometry(expandedRect);
        }
    }
}

int horizontalOverlapWidth(const QRect& first, const QRect& second)
{
    const int left = std::max(first.left(), second.left());
    const int right = std::min(first.right(), second.right());
    return std::max(0, right - left + 1);
}

void shiftGroupBoxAndContainedControls(
    QGroupBox* groupBox,
    const QRect& originalGroupRect,
    const QList<QWidget*>& controls,
    int deltaY)
{
    if (groupBox == nullptr || deltaY <= 0) {
        return;
    }

    QRect groupRect = groupBox->geometry();
    groupRect.translate(0, deltaY);
    groupBox->setGeometry(groupRect);

    for (QWidget* widget : controls) {
        if (widget == groupBox || widget->isHidden()) {
            continue;
        }
        if (!originalGroupRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(widget->geometry().center())) {
            continue;
        }

        QRect rect = widget->geometry();
        rect.translate(0, deltaY);
        widget->setGeometry(rect);
    }
}

void shiftGroupBoxAndContainedControlsRight(
    QGroupBox* groupBox,
    const QRect& originalGroupRect,
    const QList<QWidget*>& controls,
    int deltaX)
{
    if (groupBox == nullptr || deltaX <= 0) {
        return;
    }

    QRect groupRect = groupBox->geometry();
    groupRect.translate(deltaX, 0);
    groupBox->setGeometry(groupRect);

    for (QWidget* widget : controls) {
        if (widget == groupBox || widget->isHidden()) {
            continue;
        }
        if (!originalGroupRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(widget->geometry().center())) {
            continue;
        }

        QRect rect = widget->geometry();
        rect.translate(deltaX, 0);
        widget->setGeometry(rect);
    }
}

void resolveGroupBoxVerticalCollisions(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    QList<QGroupBox*> groupBoxes = dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
    groupBoxes.erase(
        std::remove_if(groupBoxes.begin(), groupBoxes.end(), [](const QGroupBox* groupBox) {
            return groupBox == nullptr || groupBox->isHidden();
        }),
        groupBoxes.end());
    std::sort(groupBoxes.begin(), groupBoxes.end(), [](const QGroupBox* first, const QGroupBox* second) {
        if (first->geometry().top() == second->geometry().top()) {
            return first->geometry().left() < second->geometry().left();
        }
        return first->geometry().top() < second->geometry().top();
    });

    for (int index = 0; index < groupBoxes.size(); ++index) {
        for (int nextIndex = index + 1; nextIndex < groupBoxes.size(); ++nextIndex) {
            QGroupBox* upper = groupBoxes.at(index);
            QGroupBox* lower = groupBoxes.at(nextIndex);
            const QRect upperRect = upper->geometry();
            const QRect lowerRect = lower->geometry();
            if (lowerRect.top() <= upperRect.top()) {
                continue;
            }
            if (upperRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                    .contains(lowerRect.center())) {
                continue;
            }
            if (horizontalOverlapWidth(upperRect, lowerRect) < std::min(upperRect.width(), lowerRect.width()) / 4) {
                continue;
            }

            const int requiredTop = upperRect.bottom() + GroupBoxVerticalGapPx;
            if (lowerRect.top() >= requiredTop) {
                continue;
            }

            shiftGroupBoxAndContainedControls(lower, lowerRect, controls, requiredTop - lowerRect.top());
        }
    }
}

void refineAboutDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("ABOUT")) {
        return;
    }

    const QPixmap logo(QStringLiteral(":/cardstack/logo-wide.png"));
    if (!logo.isNull()) {
        for (int controlId : {2100, 2101}) {
            if (auto* preview = dynamic_cast<PicturePreview*>(directControlById(dialog, controlId))) {
                preview->setPixmap(logo);
            }
        }
    }
}

void refinePrintDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("PRINT")) {
        return;
    }

    setDirectControlWidth(dialog, Control::PrintCopyCount, ShortNumericEditWidthPx);
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        if (widget->property("uiControlClass").toString() != QStringLiteral("MicroScroll")) {
            continue;
        }
        QRect rect = widget->geometry();
        rect.setWidth(MicroScrollWidthPx);
        rect.setHeight(std::max(rect.height(), widget->minimumSizeHint().height()));
        widget->setMinimumWidth(MicroScrollWidthPx);
        widget->setMaximumWidth(MicroScrollWidthPx);
        widget->setGeometry(rect);
    }

    if (QGroupBox* printSelectionGroup = directGroupBoxByTitle(dialog, QStringLiteral("Print selection"))) {
        QRect selectionRect = printSelectionGroup->geometry();
        selectionRect.setWidth(std::max(selectionRect.width(), 298));
        selectionRect.setHeight(std::max(selectionRect.height(), 130));
        printSelectionGroup->setGeometry(selectionRect);

        const int radioLeft = selectionRect.left() + 162;
        const int firstRadioTop = selectionRect.top() + 42;
        const std::pair<int, int> printRadios[] = {
            {Control::PrintThisCard, firstRadioTop},
            {Control::PrintAllCards, firstRadioTop + 30},
            {Control::PrintSelectedCards, firstRadioTop + 60},
        };
        for (const auto& [id, top] : printRadios) {
            if (QWidget* radio = directControlById(dialog, id)) {
                QRect rect = radio->geometry();
                rect.moveTo(radioLeft, top);
                rect.setWidth(std::max(rect.width(), radio->sizeHint().width()));
                radio->setGeometry(rect);
            }
        }

        if (QGroupBox* summaryGroup = directGroupBoxByTitle(dialog, QStringLiteral("Summary of selected cards"))) {
            const int oldTop = summaryGroup->geometry().top();
            QRect summaryRect = summaryGroup->geometry();
            summaryRect.setWidth(std::max(summaryRect.width(), selectionRect.width()));
            summaryRect.moveTop(selectionRect.bottom() + 12);
            summaryGroup->setGeometry(summaryRect);
            const int deltaY = summaryRect.top() - oldTop;
            for (int controlId : {Control::PrintSummary1, Control::PrintSummary2, Control::PrintSummary3}) {
                if (QWidget* label = directControlById(dialog, controlId)) {
                    QRect rect = label->geometry();
                    rect.translate(0, deltaY);
                    rect.setWidth(std::max(rect.width(), summaryRect.width() - 32));
                    label->setGeometry(rect);
                }
            }
        }
    }

    QGroupBox* summaryGroup = directGroupBoxByTitle(dialog, QStringLiteral("Summary of selected cards"));
    QWidget* defineSearchButton = directControlById(dialog, Control::PrintDefineSearch);
    if (summaryGroup != nullptr && defineSearchButton != nullptr) {
        QRect buttonRect = defineSearchButton->geometry();
        const int requiredLeft = summaryGroup->geometry().right() + DialogControlGapPx;
        if (buttonRect.left() < requiredLeft) {
            buttonRect.moveLeft(requiredLeft);
            defineSearchButton->setGeometry(buttonRect);
        }
    }
    dialog->resize(std::max(dialog->width(), 438), std::max(dialog->height(), 276));
}

void refinePrintPreviewDialog(QDialog* dialog)
{
    QWidget* canvas = directControlById(dialog, Control::PreviewCanvas);
    QWidget* pageStatus = directControlById(dialog, Control::PreviewPageStatus);
    if (canvas == nullptr || pageStatus == nullptr) {
        return;
    }

    auto setPreviewButtonGeometry = [](QWidget* button, const QRect& rect) {
        if (button == nullptr) {
            return;
        }
        button->setMinimumWidth(rect.width());
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setGeometry(rect);
    };

    constexpr int PreviewCanvasLeft = 6;
    constexpr int PreviewCanvasTop = 31;
    constexpr int PreviewCanvasWidth = 340;
    constexpr int PreviewCanvasHeight = 240;
    constexpr int PreviewCommandLeft = 360;
    constexpr int PreviewCommandWidth = 150;
    constexpr int PreviewCommandHeight = 26;
    constexpr int PreviewDialogWidth = PreviewCommandLeft + PreviewCommandWidth + DialogOuterMarginPx;
    constexpr int PreviewDialogHeight = PreviewCanvasTop + PreviewCanvasHeight + DialogOuterMarginPx;

    pageStatus->setGeometry(9, 7, 118, 18);
    if (QWidget* title = directControlById(dialog, Control::PreviewTitle)) {
        title->setGeometry(132, 7, PreviewDialogWidth - 132 - DialogOuterMarginPx, 18);
    }
    canvas->setGeometry(PreviewCanvasLeft, PreviewCanvasTop, PreviewCanvasWidth, PreviewCanvasHeight);
    setPreviewButtonGeometry(directControlById(dialog, Control::Ok), QRect(PreviewCommandLeft, 31, PreviewCommandWidth, PreviewCommandHeight));
    setPreviewButtonGeometry(directControlById(dialog, Control::Cancel), QRect(PreviewCommandLeft, 63, PreviewCommandWidth, PreviewCommandHeight));
    setPreviewButtonGeometry(directControlById(dialog, Control::PreviewNextPage), QRect(PreviewCommandLeft, 137, PreviewCommandWidth, PreviewCommandHeight));
    setPreviewButtonGeometry(directControlById(dialog, Control::PreviewFirstPage), QRect(PreviewCommandLeft, 169, PreviewCommandWidth, PreviewCommandHeight));
    dialog->resize(PreviewDialogWidth, PreviewDialogHeight);
}

void refineDesignReportsDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("DESIGNREPORTS")) {
        return;
    }

    const int actionTop = 210;
    int nextLeft = 18;
    for (int controlId : {404, 403, 401}) {
        QWidget* button = directControlById(dialog, controlId);
        if (button == nullptr) {
            continue;
        }
        QRect rect = button->geometry();
        rect.moveLeft(nextLeft);
        rect.moveTop(actionTop);
        rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
        button->setGeometry(rect);
        nextLeft = rect.right() + DialogControlGapPx;
    }

    QWidget* listBox = directControlById(dialog, 402);
    if (listBox != nullptr) {
        QRect rect = listBox->geometry();
        rect.setHeight(std::max(rect.height(), actionTop - rect.top() - 10));
        listBox->setGeometry(rect);
    }

    dialog->resize(std::max(dialog->width(), 392), std::max(dialog->height(), actionTop + 42));
}

void refineDialDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("DIAL")) {
        return;
    }

    if (auto* statusLabel = qobject_cast<QLabel*>(directControlById(dialog, 1416))) {
        statusLabel->setText(QObject::tr("Ready to dial."));
        QRect rect = statusLabel->geometry();
        rect.moveTo(18, 18);
        rect.setWidth(std::max(rect.width(), 260));
        rect.setHeight(std::max(rect.height(), statusLabel->sizeHint().height()));
        statusLabel->setGeometry(rect);
    }

    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        auto* label = qobject_cast<QLabel*>(widget);
        if (label == nullptr ||
            widget->property("originalControlId").toInt() != 65535 ||
            !plainVisibleText(label->text()).trimmed().isEmpty()) {
            continue;
        }
        widget->hide();
    }

    if (QWidget* ok = directControlById(dialog, Control::Ok)) {
        QRect rect = ok->geometry();
        rect.moveTop(58);
        ok->setGeometry(rect);
    }
    if (QWidget* cancel = directControlById(dialog, Control::Cancel)) {
        QRect rect = cancel->geometry();
        rect.moveTop(58);
        cancel->setGeometry(rect);
    }
}

void refineSecurityDialog(QDialog* dialog)
{
    const QString dialogName = dialog->property("legacyDialogName").toString();
    const QSet<QString> securityDialogs = {
        QStringLiteral("ADDSECURITY"),
        QStringLiteral("GETPASSWORD"),
        QStringLiteral("REMOVESECURITY"),
        QStringLiteral("VERIFYPASSWORD"),
        QStringLiteral("SETADMINPASS"),
        QStringLiteral("VERIFYADMINPASS"),
        QStringLiteral("GETADMINPASS"),
    };
    if (!securityDialogs.contains(dialogName)) {
        return;
    }

    QWidget* passwordEdit = directControlById(dialog, Control::SecurityPassword);
    if (passwordEdit == nullptr) {
        return;
    }

    QRect editRect = passwordEdit->geometry();
    editRect.setWidth(92);

    const QList<QLabel*> labels = dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
    const QLabel* promptLabel = nullptr;
    int bestDistance = 1000000;
    for (const QLabel* label : labels) {
        if (label->isHidden() || label->geometry().left() >= editRect.left()) {
            continue;
        }
        const QString labelText = plainVisibleText(label->text());
        if (!labelText.contains(QStringLiteral("password"), Qt::CaseInsensitive)) {
            continue;
        }
        const int verticalDistance = std::abs(label->geometry().center().y() - editRect.center().y());
        if (verticalDistance < bestDistance) {
            bestDistance = verticalDistance;
            promptLabel = label;
        }
    }

    if (promptLabel != nullptr) {
        editRect.moveLeft(promptLabel->geometry().right() + DialogControlGapPx);
    }
    passwordEdit->setMinimumWidth(editRect.width());
    passwordEdit->setMaximumWidth(editRect.width());
    passwordEdit->setGeometry(editRect);
}

void refineGetUserNameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("GETUSERNAME")) {
        return;
    }

    for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (plainVisibleText(label->text()).startsWith(QStringLiteral("To use the network version"))) {
            label->setWordWrap(true);
            label->setGeometry(18, 16, 268, 52);
            break;
        }
    }

    if (QWidget* userLabel = directControlById(dialog, 511)) {
        userLabel->setGeometry(18, 82, 102, userLabel->height());
    }
    if (QWidget* userEdit = directControlById(dialog, 510)) {
        QRect rect = userEdit->geometry();
        rect.moveTo(126, 78);
        rect.setWidth(160);
        userEdit->setGeometry(rect);
    }
    if (QWidget* storeCheck = directControlById(dialog, 512)) {
        QRect rect = storeCheck->geometry();
        rect.moveTo(24, 110);
        rect.setWidth(std::max(rect.width(), storeCheck->sizeHint().width()));
        storeCheck->setGeometry(rect);
    }

    int buttonLeft = 18;
    for (int controlId : {Control::Ok, Control::Cancel, Control::Help}) {
        if (QWidget* button = directControlById(dialog, controlId)) {
            QRect rect = button->geometry();
            rect.moveTo(buttonLeft, 142);
            rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
            button->setGeometry(rect);
            buttonLeft = rect.right() + DialogControlGapPx;
        }
    }

    dialog->resize(304, 184);
}

void refineAdminPasswordDialog(QDialog* dialog)
{
    const QString dialogName = dialog->property("legacyDialogName").toString();
    if (dialogName != QStringLiteral("GETADMINPASS") &&
        dialogName != QStringLiteral("VERIFYADMINPASS") &&
        dialogName != QStringLiteral("SETADMINPASS")) {
        return;
    }

    for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        const QString text = plainVisibleText(label->text());
        if (text.contains(QStringLiteral("requires the current Administration Password"))) {
            label->setWordWrap(true);
            label->setAlignment(Qt::AlignCenter);
            label->setGeometry(19, 8, 300, 40);
        }
    }

    if (dialogName == QStringLiteral("GETADMINPASS")) {
        if (QWidget* filenameLabel = directControlById(dialog, 513)) {
            QRect rect = filenameLabel->geometry();
            rect.moveTo(24, 58);
            filenameLabel->setGeometry(rect);
        }
        if (QWidget* filenameValue = directControlById(dialog, 505)) {
            QRect rect = filenameValue->geometry();
            rect.moveTo(136, 58);
            rect.setWidth(276);
            filenameValue->setGeometry(rect);
        }
        if (QWidget* passwordLabel = directControlById(dialog, 511)) {
            QRect rect = passwordLabel->geometry();
            rect.moveTo(24, 82);
            rect.setWidth(228);
            rect.setHeight(28);
            passwordLabel->setGeometry(rect);
            if (auto* label = qobject_cast<QLabel*>(passwordLabel)) {
                label->setWordWrap(true);
            }
        }
        if (QWidget* edit = directControlById(dialog, Control::SecurityPassword)) {
            QRect rect = edit->geometry();
            rect.moveTo(268, 78);
            rect.setWidth(132);
            edit->setMinimumWidth(rect.width());
            edit->setMaximumWidth(rect.width());
            edit->setGeometry(rect);
        }
        int buttonLeft = 74;
        for (int controlId : {Control::Ok, Control::Cancel}) {
            if (QWidget* button = directControlById(dialog, controlId)) {
                QRect rect = button->geometry();
                rect.moveTo(buttonLeft, 112);
                rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
                button->setGeometry(rect);
                buttonLeft = rect.right() + DialogControlGapPx;
            }
        }
        dialog->resize(432, 156);
    }
}

void refineSortDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("SORT")) {
        return;
    }

    const auto moveControl = [](QWidget* widget, int deltaY) {
        if (widget == nullptr) {
            return;
        }
        QRect rect = widget->geometry();
        rect.translate(0, deltaY);
        widget->setGeometry(rect);
    };

    const QList<QLabel*> labels = dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
    for (QLabel* label : labels) {
        const QString text = plainVisibleText(label->text());
        if (text == QStringLiteral("Level 2")) {
            moveControl(label, 10);
        } else if (text == QStringLiteral("Level 3")) {
            moveControl(label, 20);
        }
    }

    moveControl(directControlById(dialog, Control::SortFieldLevel2), 10);
    moveControl(directControlById(dialog, Control::SortFieldLevel3), 20);
}

void refineDefineFormDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("DEFINEFORM")) {
        return;
    }

    for (int controlId : {Control::DefineFormCard, Control::DefineFormLabel, Control::DefineFormReport}) {
        if (QWidget* radio = directControlById(dialog, controlId)) {
            QRect rect = radio->geometry();
            rect.moveTop(0);
            rect.setWidth(std::max(rect.width(), radio->sizeHint().width()));
            radio->setGeometry(rect);
        }
    }

    constexpr int NumericFieldWidthPx = 48;
    constexpr int CountFieldWidthPx = 36;
    const int numericControls[] = {
        Control::DefineFormHeight,
        Control::DefineFormWidth,
        Control::DefineFormMarginTop,
        Control::DefineFormMarginLeft,
        Control::DefineFormMarginBottom,
        Control::DefineFormMarginRight,
        Control::DefineFormHorizontalGutter,
        Control::DefineFormVerticalGutter,
    };
    const int countControls[] = {
        Control::DefineFormColumns,
        Control::DefineFormRows,
    };

    const auto setControlWidth = [dialog](int controlId, int width) {
        QWidget* widget = directControlById(dialog, controlId);
        if (widget == nullptr) {
            return;
        }
        QRect rect = widget->geometry();
        rect.setWidth(width);
        if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setMinimumWidth(width);
            spinBox->setMaximumWidth(width);
            spinBox->setRange(-9999, 9999);
            spinBox->setValue(0);
            spinBox->setFocusPolicy(Qt::NoFocus);
            if (auto* editor = spinBox->findChild<QLineEdit*>()) {
                editor->hide();
            }
            rect.setHeight(std::max(rect.height(), spinBox->sizeHint().height()));
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setMinimumWidth(width);
            lineEdit->setMaximumWidth(width);
        }
        widget->setGeometry(rect);
    };

    for (int controlId : numericControls) {
        setControlWidth(controlId, NumericFieldWidthPx);
    }
    for (int controlId : countControls) {
        setControlWidth(controlId, CountFieldWidthPx);
    }

    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        if (widget->property("uiControlClass").toString() == QStringLiteral("MicroScroll")) {
            QRect rect = widget->geometry();
            rect.setWidth(MicroScrollWidthPx);
            rect.setHeight(std::max(rect.height(), widget->minimumSizeHint().height()));
            widget->setMinimumWidth(MicroScrollWidthPx);
            widget->setMaximumWidth(MicroScrollWidthPx);
            if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
                spinBox->setRange(-9999, 9999);
                spinBox->setValue(0);
                spinBox->setFocusPolicy(Qt::NoFocus);
            }
            widget->setGeometry(rect);
        }
    }

    const auto placeNumericRowAfterLabel = [dialog, &controls](const QString& labelText, int editId, int spinId) {
        QLabel* rowLabel = nullptr;
        for (QWidget* widget : controls) {
            auto* label = qobject_cast<QLabel*>(widget);
            if (label == nullptr || label->isHidden() || plainVisibleText(label->text()) != labelText) {
                continue;
            }
            rowLabel = label;
            break;
        }
        QWidget* edit = directControlById(dialog, editId);
        QWidget* spin = directControlById(dialog, spinId);
        if (rowLabel == nullptr || edit == nullptr || spin == nullptr) {
            return;
        }

        QRect editRect = edit->geometry();
        editRect.moveLeft(rowLabel->geometry().right() + DialogControlGapPx);
        edit->setGeometry(editRect);

        QRect spinRect = spin->geometry();
        spinRect.moveLeft(editRect.right() + 1);
        spin->setGeometry(spinRect);
    };
    placeNumericRowAfterLabel(QStringLiteral("Width:"), Control::DefineFormWidth, Control::DefineFormWidthSpin);
    placeNumericRowAfterLabel(QStringLiteral("Height:"), Control::DefineFormHeight, Control::DefineFormHeightSpin);
    placeNumericRowAfterLabel(QStringLiteral("Top:"), Control::DefineFormMarginTop, Control::DefineFormMarginTopSpin);
    placeNumericRowAfterLabel(QStringLiteral("Left:"), Control::DefineFormMarginLeft, Control::DefineFormMarginLeftSpin);
    placeNumericRowAfterLabel(QStringLiteral("Right:"), Control::DefineFormMarginRight, Control::DefineFormMarginRightSpin);
    placeNumericRowAfterLabel(QStringLiteral("Bottom:"), Control::DefineFormMarginBottom, Control::DefineFormMarginBottomSpin);

    if (QWidget* landscape = directControlById(dialog, Control::DefineFormLandscape)) {
        QRect landscapeRect = landscape->geometry();
        landscapeRect.setWidth(std::max(landscapeRect.width(), landscape->sizeHint().width()));
        landscape->setGeometry(landscapeRect);
        for (QWidget* widget : controls) {
            auto* groupBox = qobject_cast<QGroupBox*>(widget);
            if (groupBox == nullptr || !groupBox->geometry().intersects(landscapeRect)) {
                continue;
            }
            QRect groupRect = groupBox->geometry();
            groupRect.setRight(std::max(groupRect.right(), landscapeRect.right() + 4));
            groupBox->setGeometry(groupRect);
        }
    }

    QGroupBox* marginsGroup = directGroupBoxByTitle(dialog, QStringLiteral("Margins"));
    auto* countGroup = qobject_cast<QGroupBox*>(directControlById(dialog, Control::DefineFormCountGroup));
    QGroupBox* sampleGroup = directGroupBoxByTitle(dialog, QStringLiteral("Sample"));

    if (marginsGroup != nullptr) {
        QRect marginsRect = marginsGroup->geometry();
        marginsRect.setWidth(std::max(marginsRect.width(), 260));
        marginsRect.setHeight(std::max(marginsRect.height(), 116));
        marginsGroup->setGeometry(marginsRect);

        if (QWidget* verticalLabel = directControlById(dialog, 331)) {
            QRect rect = verticalLabel->geometry();
            rect.moveLeft(marginsRect.left() + 14);
            rect.setWidth(std::max(rect.width(), verticalLabel->sizeHint().width()));
            verticalLabel->setGeometry(rect);

            if (QWidget* verticalEdit = directControlById(dialog, Control::DefineFormVerticalGutter)) {
                QRect editRect = verticalEdit->geometry();
                editRect.moveLeft(rect.right() + DialogControlGapPx);
                verticalEdit->setGeometry(editRect);
                if (QWidget* verticalSpin = directControlById(dialog, Control::DefineFormVerticalGutterSpin)) {
                    QRect spinRect = verticalSpin->geometry();
                    spinRect.moveLeft(editRect.right() + 1);
                    verticalSpin->setGeometry(spinRect);
                }
            }
        }

        if (QWidget* horizontalLabel = directControlById(dialog, 332)) {
            QWidget* verticalSpin = directControlById(dialog, Control::DefineFormVerticalGutterSpin);
            QRect rect = horizontalLabel->geometry();
            rect.moveLeft((verticalSpin == nullptr ? marginsRect.left() + 118 : verticalSpin->geometry().right() + 18));
            rect.setWidth(std::max(rect.width(), horizontalLabel->sizeHint().width()));
            horizontalLabel->setGeometry(rect);

            if (QWidget* horizontalEdit = directControlById(dialog, Control::DefineFormHorizontalGutter)) {
                QRect editRect = horizontalEdit->geometry();
                editRect.moveLeft(rect.right() + DialogControlGapPx);
                horizontalEdit->setGeometry(editRect);
                if (QWidget* horizontalSpin = directControlById(dialog, Control::DefineFormHorizontalGutterSpin)) {
                    QRect spinRect = horizontalSpin->geometry();
                    spinRect.moveLeft(editRect.right() + 1);
                    horizontalSpin->setGeometry(spinRect);
                    marginsRect.setRight(std::max(marginsRect.right(), spinRect.right() + GroupBoxInnerMarginPx));
                    marginsGroup->setGeometry(marginsRect);
                }
            }
        }
    }

    if (countGroup != nullptr && marginsGroup != nullptr) {
        QRect countRect = countGroup->geometry();
        countRect.moveTop(marginsGroup->geometry().bottom() + 12);
        countRect.setWidth(std::max(countRect.width(), marginsGroup->geometry().width()));
        countGroup->setGeometry(countRect);

        const int rowLabelLeft = countRect.left() + 14;
        const int countRowTop = countRect.top() + 22;
        if (QWidget* rowsLabel = directControlById(dialog, Control::DefineFormRowsLabel)) {
            QRect rect = rowsLabel->geometry();
            rect.moveLeft(rowLabelLeft);
            rect.moveTop(countRowTop + 3);
            rowsLabel->setGeometry(rect);
        }
        if (QWidget* rowsEdit = directControlById(dialog, Control::DefineFormRows)) {
            QRect rect = rowsEdit->geometry();
            rect.moveLeft(rowLabelLeft + 56);
            rect.moveTop(countRowTop);
            rowsEdit->setGeometry(rect);
            if (QWidget* rowsSpin = directControlById(dialog, Control::DefineFormRowsSpin)) {
                QRect spinRect = rowsSpin->geometry();
                spinRect.moveLeft(rect.right() + 1);
                spinRect.moveTop(rect.top());
                rowsSpin->setGeometry(spinRect);
            }
        }

        const int columnsLabelLeft = countRect.left() + 132;
        if (QWidget* columnsLabel = directControlById(dialog, Control::DefineFormColumnsLabel)) {
            QRect rect = columnsLabel->geometry();
            rect.moveLeft(columnsLabelLeft);
            rect.moveTop(countRowTop + 3);
            rect.setWidth(std::max(rect.width(), columnsLabel->sizeHint().width()));
            columnsLabel->setGeometry(rect);
        }
        if (QWidget* columnsEdit = directControlById(dialog, Control::DefineFormColumns)) {
            QWidget* columnsLabel = directControlById(dialog, Control::DefineFormColumnsLabel);
            QRect rect = columnsEdit->geometry();
            rect.moveLeft(columnsLabel == nullptr ? columnsLabelLeft + 72 : columnsLabel->geometry().right() + DialogControlGapPx);
            rect.moveTop(countRowTop);
            columnsEdit->setGeometry(rect);
            if (QWidget* columnsSpin = directControlById(dialog, Control::DefineFormColumnsSpin)) {
                QRect spinRect = columnsSpin->geometry();
                spinRect.moveLeft(rect.right() + 1);
                spinRect.moveTop(rect.top());
                columnsSpin->setGeometry(spinRect);
                countRect.setRight(std::max(countRect.right(), spinRect.right() + GroupBoxInnerMarginPx));
                countGroup->setGeometry(countRect);
            }
        }
    }

    if (sampleGroup != nullptr && marginsGroup != nullptr) {
        const int sampleLeft = marginsGroup->geometry().right() + 24;
        QRect sampleRect = sampleGroup->geometry();
        sampleRect.moveLeft(sampleLeft);
        sampleRect.moveTop(std::max(48, sampleRect.top()));
        sampleRect.setHeight(std::max(sampleRect.height(), 188));
        sampleGroup->setGeometry(sampleRect);

        QWidget* pict = directControlById(dialog, Control::DefineFormSample);
        if (pict != nullptr && pict != sampleGroup) {
            QRect pictRect = pict->geometry();
            pictRect.moveLeft(sampleRect.left() + 14);
            pictRect.moveTop(sampleRect.top() + 22);
            pictRect.setWidth(std::max(pictRect.width(), sampleRect.width() - 28));
            pictRect.setHeight(std::max(pictRect.height(), sampleRect.height() - 34));
            pict->setGeometry(pictRect);
        }

        const int buttonLeft = sampleRect.right() + 24;
        for (int controlId : {Control::Ok, Control::Cancel, Control::Help}) {
            QWidget* button = directControlById(dialog, controlId);
            if (button == nullptr) {
                continue;
            }
            QRect rect = button->geometry();
            rect.moveLeft(buttonLeft);
            button->setGeometry(rect);
        }
    }
}

void refinePhoneConfigDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("PHNDEF")) {
        return;
    }

    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        const int controlId = widget->property("originalControlId").toInt();
        const QString text = qobject_cast<QAbstractButton*>(widget) != nullptr
            ? plainVisibleText(qobject_cast<QAbstractButton*>(widget)->text())
            : QString();
        const bool isLegacyModemControl = controlId == 1428 ||
            text == QStringLiteral("Port") ||
            text == QStringLiteral("Dial Method") ||
            text == QStringLiteral("Initialization") ||
            text.startsWith(QStringLiteral("COM")) ||
            text == QStringLiteral("Tone") ||
            text == QStringLiteral("Pulse") ||
            text == QStringLiteral("Use default") ||
            text == QStringLiteral("Use custom");

        if (isLegacyModemControl) {
            widget->hide();
            continue;
        }
    }

    setDirectControlWidth(dialog, Control::PhoneLongDistancePrefix, ShortNumericEditWidthPx);
    setDirectControlWidth(dialog, Control::PhoneOutsideLinePrefix, ShortNumericEditWidthPx);
    setDirectControlWidth(dialog, Control::PhoneLocalAreaCode, ShortNumericEditWidthPx);

    QGroupBox* defaultsGroup = directGroupBoxByTitle(dialog, QStringLiteral("Dialing defaults"));
    QGroupBox* prefixesGroup = directGroupBoxByTitle(dialog, QStringLiteral("Dialing prefixes"));
    QGroupBox* quickDialsGroup = directGroupBoxByTitle(dialog, QStringLiteral("Quick dials"));

    if (defaultsGroup != nullptr) {
        defaultsGroup->setGeometry(QRect(12, 12, 220, 88));
    }
    if (prefixesGroup != nullptr) {
        prefixesGroup->setGeometry(QRect(256, 12, 330, 88));
    }
    if (quickDialsGroup != nullptr) {
        quickDialsGroup->setGeometry(QRect(12, 116, 574, 150));
    }

    const QVector<std::pair<int, QPoint>> checkboxPositions = {
        {Control::PhoneLongDistance, QPoint(24, 34)},
        {Control::PhoneOutsideLine, QPoint(24, 58)},
        {Control::PhoneLogCall, QPoint(24, 82)},
    };
    for (const auto& entry : checkboxPositions) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }

    const QVector<std::pair<QString, QPoint>> prefixLabelPositions = {
        {QStringLiteral("Long distance prefix"), QPoint(270, 36)},
        {QStringLiteral("Outside line prefix"), QPoint(270, 60)},
        {QStringLiteral("Local area code"), QPoint(270, 84)},
    };
    for (QWidget* widget : controls) {
        auto* label = qobject_cast<QLabel*>(widget);
        if (label == nullptr || label->isHidden()) {
            continue;
        }
        const QString text = plainVisibleText(label->text());
        for (const auto& entry : prefixLabelPositions) {
            if (!text.contains(entry.first, Qt::CaseInsensitive)) {
                continue;
            }
            QRect rect = label->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), label->sizeHint().width()));
            label->setGeometry(rect);
        }
    }

    const QVector<std::pair<int, QPoint>> prefixEditPositions = {
        {Control::PhoneLongDistancePrefix, QPoint(520, 32)},
        {Control::PhoneOutsideLinePrefix, QPoint(520, 56)},
        {Control::PhoneLocalAreaCode, QPoint(520, 80)},
    };
    for (const auto& entry : prefixEditPositions) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            widget->setGeometry(rect);
        }
    }

    if (QWidget* quickDials = directControlById(dialog, Control::PhoneQuickDials)) {
        quickDials->setGeometry(QRect(24, 140, 550, 78));
    }
    const QVector<std::pair<int, QPoint>> quickDialButtonPositions = {
        {Control::PhoneQuickDialAdd, QPoint(72, 228)},
        {Control::PhoneQuickDialModify, QPoint(180, 228)},
        {Control::PhoneQuickDialDelete, QPoint(288, 228)},
    };
    for (const auto& entry : quickDialButtonPositions) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            widget->setGeometry(rect);
        }
    }

    const QVector<std::pair<int, QPoint>> dialogButtonPositions = {
        {Control::Ok, QPoint(612, 20)},
        {Control::Cancel, QPoint(612, 56)},
        {Control::Help, QPoint(612, 92)},
    };
    for (const auto& entry : dialogButtonPositions) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), 92));
            widget->setGeometry(rect);
        }
    }
}

void refineTemplateDataFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("TPLDATAFRAME")) {
        return;
    }

    QWidget* nameEdit = directControlById(dialog, Control::FrameText);
    QWidget* lengthEdit = directControlById(dialog, 4210);
    QWidget* lengthLabel = directControlById(dialog, 4213);
    QWidget* microScroll = directControlById(dialog, 4211);
    if (QWidget* lengthOverlay = directControlById(dialog, 4212)) {
        lengthOverlay->hide();
    }
    if (nameEdit == nullptr || lengthEdit == nullptr || lengthLabel == nullptr || microScroll == nullptr) {
        return;
    }

    QRect nameRect = nameEdit->geometry();
    nameRect.setWidth(150);
    nameEdit->setGeometry(nameRect);
    if (auto* lineEdit = qobject_cast<QLineEdit*>(nameEdit)) {
        lineEdit->setMinimumWidth(nameRect.width());
        lineEdit->setMaximumWidth(nameRect.width());
    }

    QRect labelRect = lengthLabel->geometry();
    labelRect.moveLeft(nameRect.right() + DialogControlGapPx);
    lengthLabel->setGeometry(labelRect);

    QRect lengthRect = lengthEdit->geometry();
    lengthRect.moveLeft(labelRect.left());
    lengthEdit->setGeometry(lengthRect);
    setDirectControlWidth(dialog, 4210, ShortNumericEditWidthPx);

    QRect microRect = microScroll->geometry();
    microRect.moveLeft(lengthEdit->geometry().right() + 1);
    microRect.moveTop(lengthEdit->geometry().top());
    microRect.setHeight(lengthEdit->geometry().height());
    microScroll->setGeometry(microRect);

    if (QWidget* phoneCheck = directControlById(dialog, 4208)) {
        QRect phoneRect = phoneCheck->geometry();
        phoneRect.moveTop(nameRect.bottom() + 10);
        phoneRect.setWidth(std::max(phoneRect.width(), phoneCheck->sizeHint().width()));
        phoneCheck->setGeometry(phoneRect);

        if (QWidget* showNameCheck = directControlById(dialog, 4209)) {
            QRect showRect = showNameCheck->geometry();
            showRect.moveLeft(phoneRect.left());
            showRect.moveTop(phoneRect.bottom() + DialogControlGapPx);
            showRect.setWidth(std::max(showRect.width(), showNameCheck->sizeHint().width()));
            showNameCheck->setGeometry(showRect);

            const int buttonTop = showRect.bottom() + 12;
            if (QWidget* okButton = directControlById(dialog, 1)) {
                QRect okRect = okButton->geometry();
                okRect.moveTop(buttonTop);
                okButton->setGeometry(okRect);
            }
            if (QWidget* cancelButton = directControlById(dialog, 2)) {
                QRect cancelRect = cancelButton->geometry();
                cancelRect.moveTop(buttonTop);
                cancelButton->setGeometry(cancelRect);
            }
            dialog->resize(std::max(dialog->width(), microRect.right() + 54), std::max(dialog->height(), buttonTop + 38));
        }
    }
}

void refineImportEditDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("IMPEDIT")) {
        return;
    }

    QWidget* sizeEdit = directControlById(dialog, Control::ImportEditLength);
    QWidget* notesCheck = directControlById(dialog, Control::ImportEditNotes);
    if (sizeEdit == nullptr || notesCheck == nullptr) {
        return;
    }

    QRect sizeRect = sizeEdit->geometry();
    sizeRect.setWidth(ShortNumericEditWidthPx);
    sizeEdit->setMinimumWidth(ShortNumericEditWidthPx);
    sizeEdit->setMaximumWidth(ShortNumericEditWidthPx);
    sizeEdit->setGeometry(sizeRect);

    QRect notesRect = notesCheck->geometry();
    notesRect.moveLeft(sizeRect.right() + DialogControlGapPx * 2);
    notesRect.setWidth(std::max(notesRect.width(), notesCheck->sizeHint().width() + 18));
    notesCheck->setGeometry(notesRect);
    dialog->resize(std::max(dialog->width(), notesRect.right() + 24), dialog->height());

    if (auto* fieldNameEdit = qobject_cast<QLineEdit*>(directControlById(dialog, Control::ImportEditFieldName))) {
        fieldNameEdit->setMaxLength(64);
    }
    if (auto* sampleEdit = qobject_cast<QLineEdit*>(directControlById(dialog, Control::ImportEditSample))) {
        sampleEdit->setMaxLength(64);
        sampleEdit->setReadOnly(true);
    }
    auto* lengthEdit = qobject_cast<QLineEdit*>(sizeEdit);
    auto* notesBox = qobject_cast<QCheckBox*>(notesCheck);
    if (lengthEdit != nullptr) {
        lengthEdit->setMaxLength(5);
        QObject::connect(lengthEdit, &QLineEdit::editingFinished, dialog, [lengthEdit]() {
            bool valid = false;
            const int requestedLength = lengthEdit->text().toInt(&valid);
            lengthEdit->setText(QString::number(std::clamp(valid ? requestedLength : 1, 1, 12192)));
        });
    }
    if (lengthEdit != nullptr && notesBox != nullptr) {
        const auto syncNotesState = [lengthEdit](bool notes) {
            lengthEdit->setEnabled(!notes);
        };
        QObject::connect(notesBox, &QCheckBox::toggled, dialog, syncNotesState);
        syncNotesState(notesBox->isChecked());
    }
}

void refineAddSystemBoxDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("ADDSYSTEMBOX")) {
        return;
    }

    const QVector<std::tuple<int, int, int>> categoryRows = {
        {615, 715, 14},
        {616, 716, 66},
        {617, 717, 118},
    };
    for (const auto& row : categoryRows) {
        const int radioId = std::get<0>(row);
        const int comboId = std::get<1>(row);
        const int top = std::get<2>(row);

        if (QWidget* radio = directControlById(dialog, radioId)) {
            QRect rect = radio->geometry();
            rect.moveTop(top);
            rect.setWidth(std::max(rect.width(), radio->sizeHint().width()));
            radio->setGeometry(rect);
        }
        if (QWidget* combo = directControlById(dialog, comboId)) {
            QRect rect = combo->geometry();
            rect.moveTop(top + 22);
            rect.setWidth(std::max(rect.width(), 240));
            combo->setGeometry(rect);
            if (auto* comboBox = qobject_cast<QComboBox*>(combo)) {
                comboBox->view()->setMinimumWidth(std::max(comboBox->view()->minimumWidth(), rect.width()));
            }
        }
    }

    if (QGroupBox* alignmentGroup = directGroupBoxByTitle(dialog, QStringLiteral("Alignment"))) {
        alignmentGroup->setGeometry(QRect(24, 178, 160, 118));
    }
    if (QGroupBox* styleGroup = directGroupBoxByTitle(dialog, QStringLiteral("Style"))) {
        styleGroup->setGeometry(QRect(212, 178, 160, 118));
    }
    if (QGroupBox* exampleGroup = directGroupBoxByTitle(dialog, QStringLiteral("Example"))) {
        exampleGroup->setGeometry(QRect(24, 310, 348, 62));
    }

    const QVector<std::pair<int, QPoint>> optionPositions = {
        {622, QPoint(44, 204)},
        {623, QPoint(44, 234)},
        {624, QPoint(44, 264)},
        {627, QPoint(232, 204)},
        {628, QPoint(232, 234)},
        {629, QPoint(232, 264)},
    };
    for (const auto& entry : optionPositions) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }
    if (QWidget* preview = directControlById(dialog, Control::DefineFormSample)) {
        preview->setGeometry(QRect(44, 332, 308, 24));
    }

    const QVector<std::pair<int, int>> buttons = {
        {Control::Ok, 26},
        {Control::Cancel, 58},
        {Control::Help, 96},
    };
    for (const auto& entry : buttons) {
        if (QWidget* button = directControlById(dialog, entry.first)) {
            QRect rect = button->geometry();
            rect.moveTo(398, entry.second);
            rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
            button->setGeometry(rect);
        }
    }

    dialog->resize(std::max(dialog->width(), 500), std::max(dialog->height(), 388));
}

void refineTextFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("TEXTFRAME")) {
        return;
    }

    if (auto* alignmentGroup = directGroupBoxByTitle(dialog, QStringLiteral("Alignment"))) {
        alignmentGroup->setGeometry(12, 68, 112, 104);
    }
    if (auto* fontGroup = directGroupBoxByTitle(dialog, QStringLiteral("Font style"))) {
        fontGroup->setGeometry(152, 68, 112, 104);
    }

    const std::pair<int, QPoint> alignmentControls[] = {
        {4203, QPoint(30, 94)},
        {4204, QPoint(30, 124)},
        {4205, QPoint(30, 154)},
    };
    for (const auto& [id, point] : alignmentControls) {
        if (QWidget* control = directControlById(dialog, id)) {
            QRect rect = control->geometry();
            rect.moveTo(point);
            rect.setWidth(std::max(rect.width(), control->sizeHint().width()));
            control->setGeometry(rect);
        }
    }

    const std::pair<int, QPoint> fontControls[] = {
        {4305, QPoint(170, 94)},
        {4303, QPoint(170, 124)},
        {4304, QPoint(170, 154)},
    };
    for (const auto& [id, point] : fontControls) {
        if (QWidget* control = directControlById(dialog, id)) {
            QRect rect = control->geometry();
            rect.moveTo(point);
            rect.setWidth(std::max(rect.width(), control->sizeHint().width()));
            control->setGeometry(rect);
        }
    }

    if (QWidget* textEdit = directControlById(dialog, Control::FrameText)) {
        QRect rect = textEdit->geometry();
        rect.moveTo(19, 30);
        rect.setWidth(269);
        textEdit->setGeometry(rect);
    }
    if (QWidget* okButton = directControlById(dialog, 1)) {
        QRect rect = okButton->geometry();
        rect.moveTo(304, 30);
        okButton->setGeometry(rect);
    }
    if (QWidget* cancelButton = directControlById(dialog, 2)) {
        QRect rect = cancelButton->geometry();
        rect.moveTo(304, 62);
        cancelButton->setGeometry(rect);
    }
    dialog->resize(std::max(dialog->width(), 432), std::max(dialog->height(), 190));
}

void refineTemplateTextFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("TPLTEXTFRAME")) {
        return;
    }

    QWidget* textEdit = directControlById(dialog, 4206);
    QGroupBox* alignmentGroup = directGroupBoxByTitle(dialog, QStringLiteral("Alignment"));
    if (textEdit == nullptr || alignmentGroup == nullptr) {
        return;
    }

    QRect textRect = textEdit->geometry();
    textRect.moveTop(std::max(textRect.top(), 30));
    textEdit->setGeometry(textRect);

    QRect groupRect = alignmentGroup->geometry();
    groupRect.moveTop(textEdit->geometry().bottom() + DialogControlGapPx);
    groupRect.setWidth(std::max(groupRect.width(), 360));
    alignmentGroup->setGeometry(groupRect);

    const int radioTop = groupRect.top() + 20;
    const QVector<std::pair<int, QPoint>> radios = {
        {4203, QPoint(groupRect.left() + 16, radioTop)},
        {4204, QPoint(groupRect.left() + 140, radioTop)},
        {4205, QPoint(groupRect.left() + 264, radioTop)},
    };
    for (const auto& entry : radios) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }

    const int buttonTop = alignmentGroup->geometry().bottom() + DialogControlGapPx;
    if (QWidget* ok = directControlById(dialog, Control::Ok)) {
        QRect rect = ok->geometry();
        rect.moveTop(buttonTop);
        ok->setGeometry(rect);
    }
    if (QWidget* cancel = directControlById(dialog, Control::Cancel)) {
        QRect rect = cancel->geometry();
        rect.moveTop(buttonTop);
        cancel->setGeometry(rect);
    }
    dialog->resize(std::max(dialog->width(), alignmentGroup->geometry().right() + 24), std::max(dialog->height(), buttonTop + 38));
}

void refineGroupFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("GROUPFRAME")) {
        return;
    }

    if (auto* alignmentGroup = directGroupBoxByTitle(dialog, QStringLiteral("Alignment"))) {
        alignmentGroup->setGeometry(12, 10, 112, 104);
    }
    if (auto* fontGroup = directGroupBoxByTitle(dialog, QStringLiteral("Font style"))) {
        fontGroup->setGeometry(152, 10, 112, 104);
    }

    const QVector<std::pair<int, QPoint>> alignmentButtons = {
        {4203, QPoint(30, 35)},
        {4204, QPoint(30, 65)},
        {4205, QPoint(30, 95)},
    };
    for (const auto& entry : alignmentButtons) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }

    const QVector<std::pair<int, QPoint>> styleButtons = {
        {Control::FrameUnderline, QPoint(170, 35)},
        {Control::FrameBold, QPoint(170, 65)},
        {Control::FrameItalic, QPoint(170, 95)},
    };
    for (const auto& entry : styleButtons) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }

    if (QWidget* ok = directControlById(dialog, Control::Ok)) {
        QRect rect = ok->geometry();
        rect.moveTo(304, 24);
        ok->setGeometry(rect);
    }
    if (QWidget* cancel = directControlById(dialog, Control::Cancel)) {
        QRect rect = cancel->geometry();
        rect.moveTo(304, 56);
        cancel->setGeometry(rect);
    }
    dialog->resize(std::max(dialog->width(), 432), std::max(dialog->height(), 132));
}

void refineExportDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("EXPORT")) {
        return;
    }

    const QVector<std::pair<int, QPoint>> cardScopeRadios = {
        {971, QPoint(40, 28)},
        {972, QPoint(40, 60)},
    };
    for (const auto& entry : cardScopeRadios) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            rect.setWidth(std::max(rect.width(), widget->sizeHint().width()));
            widget->setGeometry(rect);
        }
    }
    if (QGroupBox* scopeGroup = directGroupBoxByTitle(dialog, QStringLiteral("Export which cards:"))) {
        QRect rect = scopeGroup->geometry();
        for (const auto& entry : cardScopeRadios) {
            if (QWidget* widget = directControlById(dialog, entry.first)) {
                rect.setRight(std::max(rect.right(), widget->geometry().right() + GroupBoxInnerMarginPx));
                rect.setBottom(std::max(rect.bottom(), widget->geometry().bottom() + GroupBoxInnerMarginPx));
            }
        }
        rect.setBottom(std::min(rect.bottom(), 88));
        scopeGroup->setGeometry(rect);

        int listTop = rect.bottom() + DialogControlGapPx + 14;
        for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (plainVisibleText(label->text()) == QStringLiteral("Data box name:")) {
                QRect labelRect = label->geometry();
                labelRect.moveTop(rect.bottom() + DialogControlGapPx);
                label->setGeometry(labelRect);
                listTop = labelRect.bottom() + DialogControlGapPx;
                break;
            }
        }
        for (QListWidget* listWidget : dialog->findChildren<QListWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (listWidget->geometry().left() < 180) {
                QRect listRect = listWidget->geometry();
                listRect.moveTop(listTop);
                listWidget->setGeometry(listRect);
            }
        }
    }
}

void refineDataFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("DATAFRAME")) {
        return;
    }

    if (QWidget* duplicateNameEdit = directControlById(dialog, Control::FrameText)) {
        duplicateNameEdit->hide();
    }

    constexpr int CommandColumnLeft = 304;
    if (QWidget* fieldCombo = directControlById(dialog, Control::DataFrameFieldList)) {
        QRect rect = fieldCombo->geometry();
        rect.setWidth(std::max(120, CommandColumnLeft - DialogControlGapPx - rect.left()));
        fieldCombo->setGeometry(rect);
        if (auto* comboBox = qobject_cast<QComboBox*>(fieldCombo)) {
            comboBox->view()->setMinimumWidth(std::max(comboBox->view()->minimumWidth(), rect.width()));
        }
    }

    if (auto* alignmentGroup = directGroupBoxByTitle(dialog, QStringLiteral("Alignment"))) {
        alignmentGroup->setGeometry(12, 92, 112, 104);
    }
    if (auto* fontGroup = directGroupBoxByTitle(dialog, QStringLiteral("Font style"))) {
        fontGroup->setGeometry(152, 92, 112, 104);
    }

    const std::pair<int, QPoint> alignmentControls[] = {
        {4203, QPoint(30, 117)},
        {4204, QPoint(30, 147)},
        {4205, QPoint(30, 177)},
    };
    for (const auto& [id, point] : alignmentControls) {
        if (QWidget* control = directControlById(dialog, id)) {
            QRect rect = control->geometry();
            rect.moveTo(point);
            rect.setWidth(std::max(rect.width(), control->sizeHint().width()));
            control->setGeometry(rect);
        }
    }

    const std::pair<int, QPoint> fontControls[] = {
        {4305, QPoint(170, 117)},
        {4303, QPoint(170, 147)},
        {4304, QPoint(170, 177)},
    };
    for (const auto& [id, point] : fontControls) {
        if (QWidget* control = directControlById(dialog, id)) {
            QRect rect = control->geometry();
            rect.moveTo(point);
            rect.setWidth(std::max(rect.width(), control->sizeHint().width()));
            control->setGeometry(rect);
        }
    }

    if (QWidget* okButton = directControlById(dialog, Control::Ok)) {
        QRect rect = okButton->geometry();
        rect.moveTo(CommandColumnLeft, 24);
        okButton->setGeometry(rect);
    }
    if (QWidget* cancelButton = directControlById(dialog, Control::Cancel)) {
        QRect rect = cancelButton->geometry();
        rect.moveTo(CommandColumnLeft, 56);
        cancelButton->setGeometry(rect);
    }
    dialog->resize(std::max(dialog->width(), 432), std::max(dialog->height(), 214));
}

void refineFindReplaceDialog(QDialog* dialog)
{
    const QString dialogName = dialog->property("legacyDialogName").toString();
    if (dialogName != QStringLiteral("SEARCH") && dialogName != QStringLiteral("REPLACE")) {
        return;
    }

    const QVector<std::pair<int, int>> comboWidths = {
        {Control::SearchText, 300},
        {Control::SearchSecondText, 300},
        {Control::SearchAllDataBoxes, 240},
        {Control::SearchSecondAllDataBoxes, 240},
        {Control::SearchType, 180},
        {Control::SearchSecondType, 180},
    };
    for (const auto& entry : comboWidths) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.setWidth(std::min(rect.width(), entry.second));
            widget->setGeometry(rect);
        }
    }

    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    QGroupBox* topSearchGroup = nullptr;
    for (QGroupBox* groupBox : dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (groupBox->title().trimmed().isEmpty() && groupBox->geometry().top() < 24) {
            topSearchGroup = groupBox;
            break;
        }
    }
    if (topSearchGroup == nullptr) {
        return;
    }

    const QRect groupRect = topSearchGroup->geometry();
    QRect expandedSearchGroupRect = groupRect;
    expandedSearchGroupRect.setHeight(166);
    topSearchGroup->setGeometry(expandedSearchGroupRect);

    auto placeCommandButtons = [&]() {
        const int buttonLeft = topSearchGroup->geometry().right() + DialogControlGapPx * 2;
        const std::pair<int, int> commandButtons[] = {
            {Control::Ok, topSearchGroup->geometry().top() + 12},
            {Control::Cancel, topSearchGroup->geometry().top() + 44},
            {Control::Help, topSearchGroup->geometry().top() + 82},
        };
        for (const auto& [id, top] : commandButtons) {
            if (QWidget* button = directControlById(dialog, id)) {
                QRect rect = button->geometry();
                rect.moveTo(buttonLeft, top);
                rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
                button->setGeometry(rect);
            }
        }
        dialog->resize(std::max(dialog->width(), buttonLeft + 96), dialog->height());
    };
    placeCommandButtons();

    if (QWidget* searchText = directControlById(dialog, Control::SearchText)) {
        QRect rect = searchText->geometry();
        rect.moveTop(groupRect.top() + 28);
        searchText->setGeometry(rect);
    }
    if (QWidget* dataCombo = directControlById(dialog, Control::SearchAllDataBoxes)) {
        QRect rect = dataCombo->geometry();
        rect.moveTop(groupRect.top() + 80);
        dataCombo->setGeometry(rect);
    }
    if (QWidget* typeCombo = directControlById(dialog, Control::SearchType)) {
        QRect rect = typeCombo->geometry();
        rect.moveTop(groupRect.top() + 132);
        typeCombo->setGeometry(rect);
    }
    int optionColumnLeft = 232;
    if (QWidget* dataCombo = directControlById(dialog, Control::SearchAllDataBoxes)) {
        optionColumnLeft = std::max(optionColumnLeft, dataCombo->geometry().right() + DialogControlGapPx * 2);
    }
    const QVector<std::pair<int, int>> primaryCheckBoxes = {
        {Control::SearchWholeWord, groupRect.top() + 80},
        {Control::SearchCaseSensitive, groupRect.top() + 104},
        {Control::SearchSoundsLike, groupRect.top() + 128},
    };
    for (const auto& entry : primaryCheckBoxes) {
        if (QWidget* checkBox = directControlById(dialog, entry.first)) {
            QRect rect = checkBox->geometry();
            rect.moveLeft(optionColumnLeft);
            rect.moveTop(entry.second);
            rect.setWidth(checkBox->sizeHint().width() + DialogControlGapPx);
            checkBox->setGeometry(rect);
        }
    }

    for (QWidget* widget : controls) {
        if (widget == topSearchGroup ||
            widget->isHidden() ||
            qobject_cast<QGroupBox*>(widget) != nullptr ||
            qobject_cast<QPushButton*>(widget) != nullptr) {
            continue;
        }
        if (!groupRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(widget->geometry().center())) {
            continue;
        }

        if (auto* label = qobject_cast<QLabel*>(widget)) {
            const QString text = plainVisibleText(label->text());
            QRect rect = label->geometry();
            if (text == QStringLiteral("Search in data box...")) {
                rect.moveTop(groupRect.top() + 57);
                label->setGeometry(rect);
            } else if (text == QStringLiteral("Search type...")) {
                rect.moveTop(groupRect.top() + 109);
                label->setGeometry(rect);
            }
            continue;
        }

    }

    const auto placeSearchClauseRows = [&](QGroupBox* clauseGroup,
                                           QRect clauseRect,
                                           int searchTextId,
                                           int dataComboId,
                                           int typeComboId,
                                           const QVector<int>& optionIds) {
        QWidget* searchText = directControlById(dialog, searchTextId);
        QWidget* dataCombo = directControlById(dialog, dataComboId);
        QWidget* typeCombo = directControlById(dialog, typeComboId);
        QLabel* dataLabel = nullptr;
        QLabel* typeLabel = nullptr;
        for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (label->isHidden() ||
                !clauseRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                    .contains(label->geometry().center())) {
                continue;
            }
            const QString text = plainVisibleText(label->text());
            dataLabel = text == QStringLiteral("Search in data box...") ? label : dataLabel;
            typeLabel = text == QStringLiteral("Search type...") ? label : typeLabel;
        }
        if (searchText == nullptr || dataCombo == nullptr || typeCombo == nullptr || dataLabel == nullptr || typeLabel == nullptr) {
            return clauseRect;
        }

        QRect rect = dataLabel->geometry();
        rect.moveTop(searchText->geometry().bottom() + 6);
        dataLabel->setGeometry(rect);
        rect = dataCombo->geometry();
        rect.moveTop(dataLabel->geometry().bottom() + 4);
        dataCombo->setGeometry(rect);
        rect = typeLabel->geometry();
        rect.moveTop(dataCombo->geometry().bottom() + 6);
        typeLabel->setGeometry(rect);
        rect = typeCombo->geometry();
        rect.moveTop(typeLabel->geometry().bottom() + 4);
        typeCombo->setGeometry(rect);

        int optionTop = dataCombo->geometry().top();
        int contentBottom = typeCombo->geometry().bottom();
        for (int optionId : optionIds) {
            if (QWidget* option = directControlById(dialog, optionId)) {
                QRect optionRect = option->geometry();
                optionRect.moveTop(optionTop);
                option->setGeometry(optionRect);
                optionTop = optionRect.bottom() + 5;
                contentBottom = std::max(contentBottom, optionRect.bottom());
            }
        }

        clauseRect.setBottom(contentBottom + GroupBoxInnerMarginPx * 2);
        clauseGroup->setGeometry(clauseRect);
        return clauseRect;
    };

    expandedSearchGroupRect = placeSearchClauseRows(
        topSearchGroup,
        expandedSearchGroupRect,
        Control::SearchText,
        Control::SearchAllDataBoxes,
        Control::SearchType,
        {Control::SearchWholeWord, Control::SearchCaseSensitive, Control::SearchSoundsLike});

    if (dialogName == QStringLiteral("REPLACE")) {
        const int replaceTop = expandedSearchGroupRect.bottom() + GroupBoxVerticalGapPx;
        for (QWidget* widget : controls) {
            auto* label = qobject_cast<QLabel*>(widget);
            if (label == nullptr || label->isHidden()) {
                continue;
            }
            const QString text = plainVisibleText(label->text());
            QRect rect = label->geometry();
            if (text == QStringLiteral("Replace with...")) {
                rect.moveTopLeft(QPoint(27, replaceTop));
            } else if (text == QStringLiteral("Current data")) {
                rect.moveTopLeft(QPoint(360, replaceTop));
            } else {
                continue;
            }
            label->setGeometry(rect);
        }
        if (QWidget* replacementCombo = directControlById(dialog, Control::SearchSecondText)) {
            QRect rect = replacementCombo->geometry();
            rect.moveTopLeft(QPoint(27, replaceTop + 22));
            rect.setWidth(260);
            replacementCombo->setGeometry(rect);
        }
        if (QWidget* currentData = directControlById(dialog, Control::ReplaceCurrentData)) {
            QRect rect = currentData->geometry();
            rect.moveTopLeft(QPoint(310, replaceTop + 22));
            rect.setWidth(150);
            currentData->setGeometry(rect);
        }
        const int replaceButtonLeft = 484;
        const std::pair<int, int> replaceButtons[] = {
            {4501, 18},
            {4502, 50},
            {Control::Ok, 88},
            {4500, 120},
            {Control::Cancel, 158},
            {Control::Help, 190},
        };
        for (const auto& [id, top] : replaceButtons) {
            if (QWidget* button = directControlById(dialog, id)) {
                QRect rect = button->geometry();
                rect.moveTo(replaceButtonLeft, top);
                rect.setWidth(std::max(rect.width(), button->sizeHint().width()));
                button->setGeometry(rect);
            }
        }
        if (QWidget* statusLabel = directControlById(dialog, 102)) {
            QRect rect = statusLabel->geometry();
            rect.moveTop(replaceTop + 58);
            rect.moveLeft(27);
            rect.setWidth(std::max(rect.width(), 260));
            statusLabel->setGeometry(rect);
        }
        dialog->resize(std::max(dialog->width(), replaceButtonLeft + 112), std::max(dialog->height(), 228));
        return;
    }

    QGroupBox* comparisonGroup = directGroupBoxByTitle(dialog, QStringLiteral("Additional comparison"));
    QGroupBox* directionGroup = directGroupBoxByTitle(dialog, QStringLiteral("Search direction"));
    const int comparisonTop = expandedSearchGroupRect.bottom() + GroupBoxVerticalGapPx;
    if (comparisonGroup != nullptr && comparisonGroup->geometry().top() < comparisonTop) {
        shiftGroupBoxAndContainedControls(
            comparisonGroup,
            comparisonGroup->geometry(),
            controls,
            comparisonTop - comparisonGroup->geometry().top());
    }
    if (directionGroup != nullptr && directionGroup->geometry().top() < comparisonTop) {
        shiftGroupBoxAndContainedControls(
            directionGroup,
            directionGroup->geometry(),
            controls,
            comparisonTop - directionGroup->geometry().top());
    }
    const QVector<int> comparisonButtons = {
        Control::SearchCompareNone,
        Control::SearchCompareAnd,
        Control::SearchCompareOr,
    };
    int comparisonButtonLeft = 50;
    const int comparisonButtonTop = comparisonTop + 24;
    for (int controlId : comparisonButtons) {
        if (QWidget* widget = directControlById(dialog, controlId)) {
            QRect rect = widget->geometry();
            rect.setWidth(std::max(rect.height() * 4, widget->sizeHint().width() + DialogControlGapPx));
            rect.moveTopLeft(QPoint(comparisonButtonLeft, comparisonButtonTop));
            widget->setGeometry(rect);
            comparisonButtonLeft = rect.right() + DialogControlGapPx * 3;
        }
    }
    if (comparisonGroup != nullptr) {
        QRect groupRect = comparisonGroup->geometry();
        groupRect.moveTo(10, comparisonTop);
        groupRect.setWidth(266);
        groupRect.setHeight(64);
        for (int controlId : comparisonButtons) {
            if (QWidget* widget = directControlById(dialog, controlId)) {
                groupRect.setRight(std::max(groupRect.right(), widget->geometry().right() + GroupBoxInnerMarginPx));
            }
        }
        comparisonGroup->setGeometry(groupRect);
    }
    const int directionLeft = std::max({
        286,
        comparisonGroup == nullptr ? 286 : comparisonGroup->geometry().right() + DialogControlGapPx * 2,
        topSearchGroup->geometry().right() + DialogControlGapPx * 2});
    constexpr int DirectionGroupHeightPx = 112;
    constexpr int ComparisonGroupHeightPx = 64;
    const int directionTop = comparisonTop - (DirectionGroupHeightPx - ComparisonGroupHeightPx);
    const int directionButtonLeft = directionLeft + GroupBoxInnerMarginPx * 2;
    const QVector<std::pair<int, QPoint>> directionButtons = {
        {Control::SearchDirectionBeginning, QPoint(directionButtonLeft, directionTop + 24)},
        {Control::SearchDirectionForward, QPoint(directionButtonLeft, directionTop + 54)},
        {Control::SearchDirectionBackward, QPoint(directionButtonLeft, directionTop + 84)},
    };
    for (const auto& entry : directionButtons) {
        if (QWidget* widget = directControlById(dialog, entry.first)) {
            QRect rect = widget->geometry();
            rect.moveTopLeft(entry.second);
            widget->setGeometry(rect);
        }
    }
    if (directionGroup != nullptr) {
        QRect groupRect = directionGroup->geometry();
        groupRect.moveTo(directionLeft, directionTop);
        groupRect.setWidth(250);
        groupRect.setHeight(DirectionGroupHeightPx);
        for (const auto& entry : directionButtons) {
            if (QWidget* widget = directControlById(dialog, entry.first)) {
                groupRect.setRight(std::max(groupRect.right(), widget->geometry().right() + GroupBoxInnerMarginPx));
                groupRect.setBottom(std::max(groupRect.bottom(), widget->geometry().bottom() + GroupBoxInnerMarginPx));
            }
        }
        directionGroup->setGeometry(groupRect);
    }
    if (directionGroup != nullptr) {
        if (QWidget* separator = directControlById(dialog, 313)) {
            separator->hide();
        }
        for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (plainVisibleText(label->text()).trimmed().isEmpty() &&
                directionGroup->geometry().adjusted(-2, -2, 2, 2).contains(label->geometry().center())) {
                label->hide();
            }
        }
    }

    const QList<int> lowerControls = {
        Control::SearchSecondText,
        Control::SearchSecondAllDataBoxes,
        Control::SearchSecondType,
        Control::SearchSecondWholeWord,
        Control::SearchSecondCaseSensitive,
        Control::SearchSecondSoundsLike,
    };
    for (int controlId : lowerControls) {
        QWidget* widget = directControlById(dialog, controlId);
        if (widget == nullptr || widget->isHidden()) {
            continue;
        }
    }

    QGroupBox* lowerSearchGroup = nullptr;
    int comparisonBottom = groupRect.bottom();
    for (QGroupBox* groupBox : dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (groupBox == topSearchGroup || groupBox->isHidden()) {
            continue;
        }
        if (groupBox->title().trimmed().isEmpty() && groupBox->geometry().top() > groupRect.top() + 20) {
            lowerSearchGroup = groupBox;
            continue;
        }
        if (groupBox == directionGroup) {
            continue;
        }
        comparisonBottom = std::max(comparisonBottom, groupBox->geometry().bottom());
    }

    if (lowerSearchGroup == nullptr) {
        return;
    }

    QRect lowerRect = lowerSearchGroup->geometry();
    const QRect oldLowerRect = lowerRect;
    lowerRect.moveTop(comparisonBottom + GroupBoxVerticalGapPx);
    lowerRect.setWidth(expandedSearchGroupRect.width());
    lowerRect.setHeight(166);
    lowerSearchGroup->setGeometry(lowerRect);

    const int deltaY = lowerRect.top() - oldLowerRect.top();
    for (QWidget* widget : controls) {
        if (widget == lowerSearchGroup ||
            widget->isHidden() ||
            qobject_cast<QGroupBox*>(widget) != nullptr) {
            continue;
        }
        if (!oldLowerRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(widget->geometry().center())) {
            continue;
        }
        QRect rect = widget->geometry();
        rect.translate(0, deltaY);
        widget->setGeometry(rect);
    }

    const QVector<std::tuple<int, int, int>> lowerRows = {
        {Control::SearchSecondText, Control::SearchText, lowerRect.top() + 28},
        {Control::SearchSecondAllDataBoxes, Control::SearchAllDataBoxes, lowerRect.top() + 80},
        {Control::SearchSecondType, Control::SearchType, lowerRect.top() + 132},
        {Control::SearchSecondWholeWord, Control::SearchWholeWord, lowerRect.top() + 80},
        {Control::SearchSecondCaseSensitive, Control::SearchCaseSensitive, lowerRect.top() + 104},
        {Control::SearchSecondSoundsLike, Control::SearchSoundsLike, lowerRect.top() + 128},
    };
    for (const auto& entry : lowerRows) {
        const auto [lowerId, upperId, top] = entry;
        if (QWidget* widget = directControlById(dialog, lowerId)) {
            QRect rect = widget->geometry();
            if (QWidget* upperWidget = directControlById(dialog, upperId)) {
                rect.moveLeft(upperWidget->geometry().left());
                rect.setWidth(upperWidget->geometry().width());
            }
            rect.moveTop(top);
            widget->setGeometry(rect);
        }
    }

    for (QWidget* widget : controls) {
        auto* label = qobject_cast<QLabel*>(widget);
        if (label == nullptr ||
            label->isHidden() ||
            !lowerRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(label->geometry().center())) {
            continue;
        }
        const QString text = plainVisibleText(label->text());
        QRect rect = label->geometry();
        if (text == QStringLiteral("Search in data box...")) {
            rect.moveTop(lowerRect.top() + 57);
        } else if (text == QStringLiteral("Search type...")) {
            rect.moveTop(lowerRect.top() + 109);
        } else {
            continue;
        }
        label->setGeometry(rect);
    }
    lowerRect = placeSearchClauseRows(
        lowerSearchGroup,
        lowerRect,
        Control::SearchSecondText,
        Control::SearchSecondAllDataBoxes,
        Control::SearchSecondType,
        {Control::SearchSecondWholeWord, Control::SearchSecondCaseSensitive, Control::SearchSecondSoundsLike});
    dialog->resize(
        std::max(dialog->width(), directionGroup == nullptr ? lowerRect.right() + 24 : directionGroup->geometry().right() + 24),
        lowerRect.bottom() + DialogControlGapPx * 3);
}

void normalizeSearchClauseSpacing(
    QDialog* dialog,
    int searchTextId,
    int dataComboId,
    int typeComboId,
    const QVector<int>& optionIds)
{
    QWidget* searchText = directControlById(dialog, searchTextId);
    QWidget* dataCombo = directControlById(dialog, dataComboId);
    QWidget* typeCombo = directControlById(dialog, typeComboId);
    if (searchText == nullptr || dataCombo == nullptr || typeCombo == nullptr) {
        return;
    }

    const auto nearestFollowingLabel = [dialog](const QString& text, const QWidget* preceding) {
        QLabel* result = nullptr;
        int bestDistance = std::numeric_limits<int>::max();
        for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (plainVisibleText(label->text()) != text ||
                label->geometry().top() < preceding->geometry().top()) {
                continue;
            }
            const int distance = std::abs(label->geometry().top() - preceding->geometry().bottom());
            if (distance < bestDistance) {
                result = label;
                bestDistance = distance;
            }
        }
        return result;
    };

    QLabel* dataLabel = nearestFollowingLabel(QStringLiteral("Search in data box..."), searchText);
    if (dataLabel == nullptr) {
        return;
    }
    int searchRowBottom = searchText->geometry().bottom();
    for (QComboBox* comboBox : dialog->findChildren<QComboBox*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (comboBox->isHidden() ||
            comboBox->geometry().bottom() >= dataLabel->geometry().top() ||
            comboBox->geometry().right() < dataLabel->geometry().left() ||
            comboBox->geometry().left() > dataLabel->geometry().right()) {
            continue;
        }
        searchRowBottom = std::max(searchRowBottom, comboBox->geometry().bottom());
    }
    QRect rect = dataLabel->geometry();
    rect.moveTop(searchRowBottom + 6);
    dataLabel->setGeometry(rect);
    rect = dataCombo->geometry();
    rect.moveTop(dataLabel->geometry().bottom() + 4);
    dataCombo->setGeometry(rect);

    QLabel* typeLabel = nearestFollowingLabel(QStringLiteral("Search type..."), dataCombo);
    if (typeLabel == nullptr) {
        return;
    }
    rect = typeLabel->geometry();
    rect.moveTop(dataCombo->geometry().bottom() + 6);
    typeLabel->setGeometry(rect);
    rect = typeCombo->geometry();
    rect.moveTop(typeLabel->geometry().bottom() + 4);
    typeCombo->setGeometry(rect);

    int optionTop = dataCombo->geometry().top();
    for (int optionId : optionIds) {
        if (QWidget* option = directControlById(dialog, optionId)) {
            QRect optionRect = option->geometry();
            optionRect.moveTop(optionTop);
            option->setGeometry(optionRect);
            optionTop = optionRect.bottom() + 5;
        }
    }
}

void normalizeFindReplaceSearchSpacing(QDialog* dialog)
{
    const QString dialogName = dialog->property("legacyDialogName").toString();
    if (dialogName != QStringLiteral("SEARCH") && dialogName != QStringLiteral("REPLACE")) {
        return;
    }
    normalizeSearchClauseSpacing(
        dialog,
        Control::SearchText,
        Control::SearchAllDataBoxes,
        Control::SearchType,
        {Control::SearchWholeWord, Control::SearchCaseSensitive, Control::SearchSoundsLike});
    if (dialogName == QStringLiteral("SEARCH")) {
        normalizeSearchClauseSpacing(
            dialog,
            Control::SearchSecondText,
            Control::SearchSecondAllDataBoxes,
            Control::SearchSecondType,
            {Control::SearchSecondWholeWord, Control::SearchSecondCaseSensitive, Control::SearchSecondSoundsLike});
    }
}

void refineLineFrameDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("LINEFRAME")) {
        return;
    }

    const QVector<std::pair<int, QString>> lineTargetLabels = {
        {Control::LineFrameBox, QObject::tr("Box")},
        {Control::LineFrameHorizontal, QObject::tr("Horizontal")},
        {Control::LineFrameVertical, QObject::tr("Vertical")},
    };
    const int targetLefts[] = {18, 88, 218};
    const int targetWidths[] = {62, 118, 82};
    for (int index = 0; index < lineTargetLabels.size(); ++index) {
        QWidget* widget = directControlById(dialog, lineTargetLabels.at(index).first);
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (button == nullptr) {
            continue;
        }
        button->setText(lineTargetLabels.at(index).second);
        QRect rect = button->geometry();
        rect.moveLeft(targetLefts[index]);
        rect.setWidth(std::max(targetWidths[index], button->sizeHint().width()));
        button->setGeometry(rect);
    }

    for (int bitmapId : {4313, 4314, 4315}) {
        if (QWidget* widget = directControlById(dialog, bitmapId)) {
            widget->hide();
        }
    }

    if (QWidget* lineStyle = directControlById(dialog, Control::LineFrameLineStyle)) {
        QRect rect = lineStyle->geometry();
        rect.setWidth(std::max(rect.width(), 200));
        lineStyle->setGeometry(rect);
    }
    if (QWidget* fillPattern = directControlById(dialog, Control::LineFrameFillPattern)) {
        QRect rect = fillPattern->geometry();
        rect.setWidth(std::max(rect.width(), 200));
        fillPattern->setGeometry(rect);
    }
    if (QWidget* cornerRadius = directControlById(dialog, Control::LineFrameCornerRadius)) {
        QRect rect = cornerRadius->geometry();
        rect.setWidth(ShortNumericEditWidthPx);
        cornerRadius->setMinimumWidth(ShortNumericEditWidthPx);
        cornerRadius->setMaximumWidth(ShortNumericEditWidthPx);
        cornerRadius->setGeometry(rect);
    }
    if (QWidget* cornerSpin = directControlById(dialog, Control::LineFrameCornerRadiusSpin)) {
        if (QWidget* cornerRadius = directControlById(dialog, Control::LineFrameCornerRadius)) {
            QRect rect = cornerSpin->geometry();
            rect.moveLeft(cornerRadius->geometry().right() + 1);
            cornerSpin->setGeometry(rect);
        }
    }

    QGroupBox* example = directGroupBoxByTitle(dialog, QStringLiteral("Example"));
    if (example != nullptr) {
        QRect rect = example->geometry();
        rect.moveLeft(250);
        rect.setWidth(std::max(rect.width(), 140));
        example->setGeometry(rect);
        if (QWidget* picture = directControlById(dialog, Control::LineFramePreview)) {
            QRect pictureRect = picture->geometry();
            pictureRect.moveLeft(rect.left() + 16);
            pictureRect.setWidth(std::max(pictureRect.width(), rect.width() - 32));
            picture->setGeometry(pictureRect);
        }

        const int buttonLeft = rect.right() + 24;
        for (int controlId : {Control::Ok, Control::Cancel}) {
            QWidget* button = directControlById(dialog, controlId);
            if (button == nullptr) {
                continue;
            }
            QRect buttonRect = button->geometry();
            buttonRect.moveLeft(buttonLeft);
            button->setGeometry(buttonRect);
        }
    }
}

void refineDialogGeometry(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);

    for (QWidget* widget : controls) {
        if (!isTextSizedControl(widget)) {
            continue;
        }
        if (qobject_cast<QGroupBox*>(widget) != nullptr) {
            continue;
        }

        QRect rect = widget->geometry();
        rect.setHeight(std::max(rect.height(), widget->sizeHint().height()));
        int preferredWidth = preferredTextControlWidth(widget);
        if (widget->maximumWidth() < QWIDGETSIZE_MAX) {
            preferredWidth = std::min(preferredWidth, widget->maximumWidth());
        }
        if (qobject_cast<QLabel*>(widget) != nullptr) {
            rect.setWidth(preferredWidth);
        } else {
            rect.setWidth(std::max(rect.width(), preferredWidth));
        }
        if (widget->maximumWidth() < QWIDGETSIZE_MAX) {
            rect.setWidth(std::min(rect.width(), widget->maximumWidth()));
        }
        widget->setGeometry(rect);
    }

    for (QWidget* widget : controls) {
        if (!isTextSizedControl(widget)) {
            continue;
        }
        if (qobject_cast<QLabel*>(widget) != nullptr ||
            qobject_cast<QGroupBox*>(widget) != nullptr ||
            qobject_cast<QAbstractButton*>(widget) != nullptr) {
            continue;
        }

        QRect rect = widget->geometry();
        int nearestRight = dialog->width() - DialogControlGapPx;
        for (const QWidget* other : controls) {
            if (other == widget || !other->isVisible()) {
                continue;
            }
            const QRect otherRect = other->geometry();
            if (otherRect.left() > rect.left() && rowsOverlap(rect, otherRect)) {
                nearestRight = std::min(nearestRight, otherRect.left() - DialogControlGapPx);
            }
        }

        const int maxWidth = nearestRight - rect.left();
        const int minimumWidth = qobject_cast<QComboBox*>(widget) != nullptr
            ? ComboMinimumShrunkWidthPx
            : std::min(widget->sizeHint().width(), TextMinimumShrunkWidthPx);
        if (maxWidth >= minimumWidth && rect.width() > maxWidth) {
            rect.setWidth(maxWidth);
            widget->setGeometry(rect);
        }
    }

    refineAboutDialog(dialog);
    refinePrintDialog(dialog);
    refinePrintPreviewDialog(dialog);
    refineDesignReportsDialog(dialog);
    refineDialDialog(dialog);
    refineSecurityDialog(dialog);
    refineGetUserNameDialog(dialog);
    refineAdminPasswordDialog(dialog);
    refineSortDialog(dialog);
    refineDefineFormDialog(dialog);
    refinePhoneConfigDialog(dialog);
    refineTemplateDataFrameDialog(dialog);
    refineImportEditDialog(dialog);
    refineAddSystemBoxDialog(dialog);
    refineTextFrameDialog(dialog);
    refineTemplateTextFrameDialog(dialog);
    refineGroupFrameDialog(dialog);
    refineExportDialog(dialog);
    refineDataFrameDialog(dialog);
    refineFindReplaceDialog(dialog);
    refineLineFrameDialog(dialog);
    normalizeMicroScrollAdjacency(dialog);
    expandGroupBoxesToContainChildren(dialog);
    resolveGroupBoxVerticalCollisions(dialog);
    refinePrintDialog(dialog);
    refinePrintPreviewDialog(dialog);
    refineDialDialog(dialog);
    refineFindReplaceDialog(dialog);
    normalizeFindReplaceSearchSpacing(dialog);
    expandGroupBoxesToContainChildren(dialog);

    for (QComboBox* comboBox : dialog->findChildren<QComboBox*>()) {
        if (comboBox->isHidden() || comboBox->isEditable() || comboBox->count() == 0) {
            continue;
        }
        const QFontMetrics metrics(comboBox->font());
        QString widestItem;
        for (int index = 0; index < comboBox->count(); ++index) {
            const QString item = comboBox->itemText(index);
            if (metrics.horizontalAdvance(item) > metrics.horizontalAdvance(widestItem)) {
                widestItem = item;
            }
        }
        QStyleOptionComboBox option;
        option.initFrom(comboBox);
        option.editable = false;
        option.currentText = widestItem;
        const QSize contentSize(metrics.horizontalAdvance(widestItem), metrics.height());
        const int requiredWidth = comboBox->style()->sizeFromContents(
            QStyle::CT_ComboBox,
            &option,
            contentSize,
            comboBox).width();
        if (requiredWidth > comboBox->width()) {
            QRect rect = comboBox->geometry();
            rect.setWidth(requiredWidth);
            comboBox->setGeometry(rect);
        }
    }

    QRect childrenBounds;
    for (const QWidget* widget : controls) {
        childrenBounds = childrenBounds.united(widget->geometry());
    }
    if (childrenBounds.isValid()) {
        const QSize expandedSize(
            std::max(dialog->width(), childrenBounds.right() + DialogOuterMarginPx),
            std::max(dialog->height(), childrenBounds.bottom() + DialogOuterMarginPx));
        dialog->resize(expandedSize);
        dialog->setMinimumSize(expandedSize);
    }
}

void connectStandardDialogButtons(QDialog* dialog, QWidget* widget, int controlId)
{
    auto* button = qobject_cast<QPushButton*>(widget);
    if (button == nullptr) {
        return;
    }

    if (controlId == Control::Ok) {
        QObject::connect(button, &QPushButton::clicked, dialog, &QDialog::accept);
    } else if (controlId == Control::Cancel) {
        QObject::connect(button, &QPushButton::clicked, dialog, &QDialog::reject);
    } else if (controlId == Control::Help) {
        QObject::connect(button, &QPushButton::clicked, dialog, [dialog]() {
            showCardStackHelp(dialog);
        });
    }
}

template <typename T>
T* findUiControl(QWidget* parent, int controlId)
{
    const QList<T*> controls = parent->findChildren<T*>();
    T* hiddenCandidate = nullptr;
    for (T* control : controls) {
        if (control->property("originalControlId").toInt() == controlId) {
            if (!control->isHidden()) {
                return control;
            }
            if (hiddenCandidate == nullptr) {
                hiddenCandidate = control;
            }
        }
    }

    return hiddenCandidate;
}

void setControlsEnabled(QWidget* parent, const QList<int>& controlIds, bool enabled)
{
    for (int controlId : controlIds) {
        if (QWidget* control = findUiControl<QWidget>(parent, controlId)) {
            control->setEnabled(enabled);
        }
    }
}

void setSearchSecondClauseVisible(QDialog* dialog, bool visible)
{
    QGroupBox* secondClauseGroup = nullptr;
    for (QGroupBox* groupBox : dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (groupBox->title().trimmed().isEmpty() && groupBox->geometry().top() > 120) {
            secondClauseGroup = groupBox;
            break;
        }
    }
    if (secondClauseGroup == nullptr) {
        return;
    }

    const QRect groupRect = secondClauseGroup->geometry();
    secondClauseGroup->setVisible(visible);
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : controls) {
        if (widget == secondClauseGroup || qobject_cast<QGroupBox*>(widget) != nullptr) {
            continue;
        }
        if (groupRect.adjusted(-GroupBoxInnerMarginPx, -GroupBoxInnerMarginPx, GroupBoxInnerMarginPx, GroupBoxInnerMarginPx)
                .contains(widget->geometry().center())) {
            widget->setVisible(visible);
        }
    }
}

void resizeDialogToVisibleContent(QDialog* dialog)
{
    int contentBottom = 0;
    for (QWidget* child : dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!child->isHidden()) {
            contentBottom = std::max(contentBottom, child->geometry().bottom());
        }
    }
    const int targetHeight = contentBottom + DialogOuterMarginPx;
    dialog->setMinimumHeight(targetHeight);
    dialog->resize(dialog->width(), targetHeight);
}

QButtonGroup* createExclusiveButtonGroup(QWidget* parent, const QList<int>& controlIds, int checkedControlId)
{
    auto* group = new QButtonGroup(parent);
    group->setExclusive(true);

    for (int controlId : controlIds) {
        auto* button = findUiControl<QAbstractButton>(parent, controlId);
        if (button == nullptr) {
            continue;
        }

        button->setAutoExclusive(false);
        group->addButton(button, controlId);
        if (controlId == checkedControlId) {
            button->setChecked(true);
        }
    }

    return group;
}

void populateComboBox(QWidget* parent, int controlId, const QStringList& items, bool editable)
{
    auto* comboBox = findUiControl<QComboBox>(parent, controlId);
    if (comboBox == nullptr) {
        return;
    }

    comboBox->clear();
    comboBox->addItems(items);
    comboBox->setEditable(editable);
    if (!items.isEmpty()) {
        comboBox->setCurrentIndex(0);
    }
    if (comboBox->view() != nullptr) {
        int popupWidth = comboBox->width();
        const QFontMetrics metrics(comboBox->font());
        for (const QString& item : items) {
            popupWidth = std::max(popupWidth, metrics.horizontalAdvance(item) + ComboDropDownArrowPaddingPx);
        }
        comboBox->view()->setMinimumWidth(popupWidth);
    }
}

void setControlWidth(QWidget* parent, int controlId, int width)
{
    QWidget* control = findUiControl<QWidget>(parent, controlId);
    if (control == nullptr) {
        return;
    }

    QRect rect = control->geometry();
    rect.setWidth(width);
    control->setGeometry(rect);
    control->setMinimumWidth(width);
    control->setMaximumWidth(width);
}

void populateListWidget(QWidget* parent, int controlId, const QStringList& items)
{
    auto* listWidget = findUiControl<QListWidget>(parent, controlId);
    if (listWidget == nullptr) {
        return;
    }

    listWidget->clear();
    listWidget->addItems(items);
    if (!items.isEmpty()) {
        listWidget->setCurrentRow(0);
    }
}

void setLabelText(QWidget* parent, int controlId, const QString& text)
{
    if (auto* label = findUiControl<QLabel>(parent, controlId)) {
        label->setText(text);
    }
}

QStringList fieldsWithAllDataBoxes(const UiBuilder::DialogContext& context)
{
    QStringList items;
    items.append(
        legacyString(StringId::AllDataBoxes).isEmpty()
            ? QStringLiteral("ALL DATA BOXES")
            : legacyString(StringId::AllDataBoxes));
    items.append(context.fieldNames);
    return items;
}

void setupListTransfer(
    QDialog* dialog,
    int sourceListId,
    int destinationListId,
    int addButtonId,
    int addAllButtonId,
    int removeButtonId,
    int removeAllButtonId,
    const QString& mappedPrefix = {})
{
    auto* source = findUiControl<QListWidget>(dialog, sourceListId);
    auto* destination = findUiControl<QListWidget>(dialog, destinationListId);
    if (source == nullptr || destination == nullptr) {
        return;
    }

    auto addCurrent = [source, destination, mappedPrefix]() {
        const QListWidgetItem* item = source->currentItem();
        if (item == nullptr) {
            return;
        }
        const QString value = mappedPrefix.isEmpty()
            ? item->text()
            : QStringLiteral("%1%2").arg(mappedPrefix, item->text());
        destination->addItem(value);
    };

    auto addAll = [source, destination, mappedPrefix]() {
        destination->clear();
        for (int row = 0; row < source->count(); ++row) {
            const QString value = mappedPrefix.isEmpty()
                ? source->item(row)->text()
                : QStringLiteral("%1%2").arg(mappedPrefix, source->item(row)->text());
            destination->addItem(value);
        }
    };

    auto removeCurrent = [destination]() {
        delete destination->takeItem(destination->currentRow());
    };

    auto removeAll = [destination]() {
        destination->clear();
    };

    if (auto* button = findUiControl<QAbstractButton>(dialog, addButtonId)) {
        QObject::connect(button, &QAbstractButton::clicked, dialog, addCurrent);
    }
    if (auto* button = findUiControl<QAbstractButton>(dialog, addAllButtonId)) {
        QObject::connect(button, &QAbstractButton::clicked, dialog, addAll);
    }
    if (auto* button = findUiControl<QAbstractButton>(dialog, removeButtonId)) {
        QObject::connect(button, &QAbstractButton::clicked, dialog, removeCurrent);
    }
    if (auto* button = findUiControl<QAbstractButton>(dialog, removeAllButtonId)) {
        QObject::connect(button, &QAbstractButton::clicked, dialog, removeAll);
    }

    QObject::connect(source, &QListWidget::itemDoubleClicked, dialog, [addCurrent](QListWidgetItem*) {
        addCurrent();
    });
    QObject::connect(destination, &QListWidget::itemDoubleClicked, dialog, [removeCurrent](QListWidgetItem*) {
        removeCurrent();
    });
}

QStringList searchTypeItems()
{
    QStringList items;
    for (int stringId = StringId::SearchTypeFirst; stringId <= StringId::SearchTypeLast; ++stringId) {
        items.append(legacyString(stringId));
    }
    return items;
}

void initializeNewFileDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    const QStringList sourceItems = {
        legacyString(StringId::NewFromTemplate),
        legacyString(StringId::NewFromScratch),
        legacyString(StringId::NewPatternedAfterTemplate),
        legacyString(StringId::NewPatternedAfterDeck),
    };
    populateComboBox(dialog, Control::NewFileSourceCombo, sourceItems, false);
    populateListWidget(dialog, Control::NewFileTemplateList, context.templateNames);

    auto* sourceCombo = findUiControl<QComboBox>(dialog, Control::NewFileSourceCombo);
    auto* templateList = findUiControl<QListWidget>(dialog, Control::NewFileTemplateList);
    if (sourceCombo == nullptr || templateList == nullptr) {
        return;
    }
    setControlWidth(dialog, Control::NewFileSourceCombo, NewFileSourceComboWidthPx);
    sourceCombo->view()->setMinimumWidth(NewFileSourcePopupWidthPx);

    QLabel* sourceLabel = nullptr;
    for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (plainVisibleText(label->text()) == QStringLiteral("Source:")) {
            sourceLabel = label;
            break;
        }
    }
    if (sourceLabel != nullptr) {
        QRect labelRect = sourceLabel->geometry();
        labelRect.moveTop(templateList->geometry().bottom() + DialogControlGapPx);
        sourceLabel->setGeometry(labelRect);

        QRect comboRect = sourceCombo->geometry();
        comboRect.moveTop(labelRect.bottom() + 4);
        sourceCombo->setGeometry(comboRect);
    }

    auto updateTemplateState = [dialog, sourceCombo, templateList]() {
        const int selected = sourceCombo->currentIndex();
        const bool enabled = selected == 0 || selected == 2;
        if (!enabled) {
            if (templateList->currentRow() >= 0) {
                dialog->setProperty("savedTemplateRow", templateList->currentRow());
            }
            templateList->setEnabled(false);
            return;
        }

        templateList->setEnabled(true);
        const int savedRow = dialog->property("savedTemplateRow").toInt();
        if (savedRow >= 0 && savedRow < templateList->count()) {
            templateList->setCurrentRow(savedRow);
        }
    };

    QObject::connect(sourceCombo, &QComboBox::currentIndexChanged, dialog, updateTemplateState);
    QObject::connect(templateList, &QListWidget::itemDoubleClicked, dialog, [dialog](QListWidgetItem*) {
        dialog->accept();
    });
    updateTemplateState();
}

void initializeSearchDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    populateComboBox(dialog, Control::SearchType, searchTypeItems(), false);
    populateComboBox(dialog, Control::SearchSecondType, searchTypeItems(), false);
    populateComboBox(dialog, Control::SearchAllDataBoxes, fieldsWithAllDataBoxes(context), false);
    populateComboBox(dialog, Control::SearchSecondAllDataBoxes, fieldsWithAllDataBoxes(context), false);
    populateComboBox(dialog, Control::SearchText, context.recentSearches, true);
    populateComboBox(dialog, Control::SearchSecondText, context.recentSearches, true);
    setControlWidth(dialog, Control::SearchType, 180);
    setControlWidth(dialog, Control::SearchSecondType, 180);

    createExclusiveButtonGroup(
        dialog,
        {Control::SearchCompareNone, Control::SearchCompareAnd, Control::SearchCompareOr},
        Control::SearchCompareNone);
    createExclusiveButtonGroup(
        dialog,
        {Control::SearchDirectionBeginning, Control::SearchDirectionForward, Control::SearchDirectionBackward},
        Control::SearchDirectionBeginning);
    setControlsEnabled(
        dialog,
        {Control::SearchDirectionBeginning, Control::SearchDirectionForward, Control::SearchDirectionBackward},
        context.searchDirectionAvailable);

    const QList<int> secondClauseControls = {
        Control::SearchSecondText,
        Control::SearchSecondAllDataBoxes,
        Control::SearchSecondType,
        Control::SearchSecondWholeWord,
        Control::SearchSecondCaseSensitive,
        Control::SearchSecondSoundsLike,
    };
    setControlsEnabled(dialog, secondClauseControls, false);
    setSearchSecondClauseVisible(dialog, false);

    auto updateSecondClause = [dialog, secondClauseControls]() {
        const auto* andButton = findUiControl<QAbstractButton>(dialog, Control::SearchCompareAnd);
        const auto* orButton = findUiControl<QAbstractButton>(dialog, Control::SearchCompareOr);
        const bool enabled = (andButton != nullptr && andButton->isChecked()) ||
            (orButton != nullptr && orButton->isChecked());
        setControlsEnabled(dialog, secondClauseControls, enabled);
        setSearchSecondClauseVisible(dialog, enabled);
        refineFindReplaceDialog(dialog);
        resizeDialogToVisibleContent(dialog);
    };

    for (int controlId : {Control::SearchCompareNone, Control::SearchCompareAnd, Control::SearchCompareOr}) {
        if (auto* button = findUiControl<QAbstractButton>(dialog, controlId)) {
            QObject::connect(button, &QAbstractButton::toggled, dialog, [updateSecondClause](bool) {
                updateSecondClause();
            });
        }
    }

    updateSecondClause();
}

void initializeReplaceDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    populateComboBox(dialog, Control::SearchText, context.recentSearches, true);
    populateComboBox(dialog, Control::SearchAllDataBoxes, fieldsWithAllDataBoxes(context), false);
    populateComboBox(dialog, Control::SearchType, searchTypeItems(), false);
    populateComboBox(dialog, Control::SearchSecondText, context.recentReplacements, true);
    setLabelText(dialog, Control::ReplaceStatus, QObject::tr("Ready"));
    if (auto* currentData = findUiControl<QLineEdit>(dialog, Control::ReplaceCurrentData)) {
        currentData->setReadOnly(true);
        if (auto* replaceButton = findUiControl<QAbstractButton>(dialog, Control::Ok)) {
            currentData->setFocusProxy(replaceButton);
        }
    }
}

void initializePrintDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::PrintThisCard, Control::PrintAllCards, Control::PrintSelectedCards},
        Control::PrintThisCard);

    auto* copyCount = findUiControl<QLineEdit>(dialog, Control::PrintCopyCount);
    if (copyCount != nullptr) {
        copyCount->setText(QStringLiteral("1"));
        QObject::connect(copyCount, &QLineEdit::textChanged, dialog, [copyCount](const QString& text) {
            bool valid = false;
            const int requested = text.toInt(&valid);
            const QString normalized = QString::number(std::clamp(valid ? requested : 1, 1, 10000));
            if (text == normalized) {
                return;
            }
            const QSignalBlocker blocker(copyCount);
            copyCount->setText(normalized);
            copyCount->setCursorPosition(normalized.size());
        });
    }
    if (auto* spin = findUiControl<QSpinBox>(dialog, Control::PrintCopyCountSpin)) {
        spin->setRange(-10000, 10000);
        spin->setValue(0);
        QObject::connect(spin, &QSpinBox::valueChanged, dialog, [copyCount, spin](int delta) {
            if (delta == 0 || copyCount == nullptr) {
                return;
            }
            bool valid = false;
            const int current = copyCount->text().toInt(&valid);
            copyCount->setText(QString::number(std::clamp((valid ? current : 1) + delta, 1, 10000)));
            const QSignalBlocker blocker(spin);
            spin->setValue(0);
            copyCount->setFocus();
        });
    }
    setLabelText(dialog, Control::PrintPrinterName, QObject::tr("Default printer"));
    setLabelText(
        dialog,
        Control::PrintReportName,
        context.reportNames.isEmpty() ? QObject::tr("Default Page Report") : context.reportNames.first());
    setLabelText(dialog, Control::PrintSummary1, QObject::tr("All selected records are eligible for printing."));
    setLabelText(dialog, Control::PrintSummary2, QObject::tr("Use Define Search to change the selected-card set."));
    setLabelText(dialog, Control::PrintSummary3, QObject::tr("Print Preview opens the preview dialog."));

    const auto updateScope = [dialog]() {
        const auto* selected = findUiControl<QAbstractButton>(dialog, Control::PrintSelectedCards);
        const auto* all = findUiControl<QAbstractButton>(dialog, Control::PrintAllCards);
        const bool selectedCards = selected != nullptr && selected->isChecked();
        const bool allCards = all != nullptr && all->isChecked();
        setLabelText(
            dialog,
            Control::PrintSummary1,
            selectedCards
                ? QObject::tr("Cards matching the current selection will be printed.")
                : (allCards ? QObject::tr("All cards will be printed.") : QObject::tr("The current card will be printed.")));
        setLabelText(
            dialog,
            Control::PrintSummary2,
            selectedCards ? QObject::tr("Use Define Search to change the selected-card set.") : QString());
        if (auto* defineSearch = findUiControl<QAbstractButton>(dialog, Control::PrintDefineSearch)) {
            defineSearch->setEnabled(selectedCards);
        }
    };
    for (int controlId : {Control::PrintThisCard, Control::PrintAllCards, Control::PrintSelectedCards}) {
        if (auto* button = findUiControl<QAbstractButton>(dialog, controlId)) {
            QObject::connect(button, &QAbstractButton::toggled, dialog, [updateScope](bool checked) {
                if (checked) {
                    updateScope();
                }
            });
        }
    }
    updateScope();
}

void initializeLineFrameDialog(QDialog* dialog)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::LineFrameBox, Control::LineFrameHorizontal, Control::LineFrameVertical},
        Control::LineFrameBox);

    const auto updatePreview = [dialog]() {
        QWidget* preview = findUiControl<QWidget>(dialog, Control::LineFramePreview);
        if (preview == nullptr) {
            return;
        }
        int shape = 0;
        if (const auto* horizontal = findUiControl<QAbstractButton>(dialog, Control::LineFrameHorizontal);
            horizontal != nullptr && horizontal->isChecked()) {
            shape = 1;
        } else if (const auto* vertical = findUiControl<QAbstractButton>(dialog, Control::LineFrameVertical);
                   vertical != nullptr && vertical->isChecked()) {
            shape = 2;
        }
        const auto* lineStyle = findUiControl<QComboBox>(dialog, Control::LineFrameLineStyle);
        const auto* fillPattern = findUiControl<QComboBox>(dialog, Control::LineFrameFillPattern);
        const auto* cornerRadius = findUiControl<QLineEdit>(dialog, Control::LineFrameCornerRadius);
        preview->setProperty("lineFrameSample", true);
        preview->setProperty("lineFrameShape", shape);
        preview->setProperty("lineFrameLineStyle", lineStyle == nullptr ? 0 : std::max(0, lineStyle->currentIndex()));
        preview->setProperty("lineFrameFillPattern", fillPattern == nullptr ? 0 : std::max(0, fillPattern->currentIndex()));
        preview->setProperty("lineFrameCornerRadius", cornerRadius == nullptr ? 0 : cornerRadius->text().toInt());
        preview->update();
    };

    for (int controlId : {Control::LineFrameBox, Control::LineFrameHorizontal, Control::LineFrameVertical}) {
        if (auto* button = findUiControl<QAbstractButton>(dialog, controlId)) {
            QObject::connect(button, &QAbstractButton::toggled, dialog, [updatePreview](bool checked) {
                if (checked) {
                    updatePreview();
                }
            });
        }
    }
    for (int controlId : {Control::LineFrameLineStyle, Control::LineFrameFillPattern}) {
        if (auto* combo = findUiControl<QComboBox>(dialog, controlId)) {
            QObject::connect(combo, &QComboBox::currentIndexChanged, dialog, [updatePreview](int) {
                updatePreview();
            });
        }
    }
    auto* cornerRadius = findUiControl<QLineEdit>(dialog, Control::LineFrameCornerRadius);
    if (cornerRadius != nullptr) {
        QObject::connect(cornerRadius, &QLineEdit::textChanged, dialog, [cornerRadius, updatePreview](const QString& text) {
            bool valid = false;
            const int requested = text.toInt(&valid);
            const QString normalized = QString::number(std::clamp(valid ? requested : 0, 0, 200));
            if (text != normalized) {
                const QSignalBlocker blocker(cornerRadius);
                cornerRadius->setText(normalized);
                cornerRadius->setCursorPosition(normalized.size());
            }
            updatePreview();
        });
    }
    if (auto* spin = findUiControl<QSpinBox>(dialog, Control::LineFrameCornerRadiusSpin)) {
        spin->setRange(-200, 200);
        spin->setValue(0);
        QObject::connect(spin, &QSpinBox::valueChanged, dialog, [cornerRadius, spin](int delta) {
            if (delta == 0 || cornerRadius == nullptr) {
                return;
            }
            cornerRadius->setText(QString::number(std::clamp(cornerRadius->text().toInt() + delta * 5, 0, 200)));
            const QSignalBlocker blocker(spin);
            spin->setValue(0);
            cornerRadius->setFocus();
        });
    }
    updatePreview();
}

void initializeSortDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    QStringList sortFields;
    sortFields.append(QString());
    sortFields.append(context.fieldNames);
    populateComboBox(dialog, Control::SortFieldLevel1, sortFields, false);
    populateComboBox(dialog, Control::SortFieldLevel2, sortFields, false);
    populateComboBox(dialog, Control::SortFieldLevel3, sortFields, false);

    auto* secondLevel = findUiControl<QComboBox>(dialog, Control::SortFieldLevel2);
    auto* thirdLevel = findUiControl<QComboBox>(dialog, Control::SortFieldLevel3);
    if (secondLevel != nullptr && thirdLevel != nullptr) {
        QObject::connect(secondLevel, qOverload<int>(&QComboBox::activated), dialog, [thirdLevel](int index) {
            if (index == 0) {
                thirdLevel->setFocus();
            }
        });
    }
}

void initializeMergeDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    createExclusiveButtonGroup(dialog, {Control::AllCards, Control::SelectedCards}, Control::AllCards);
    setLabelText(dialog, Control::MergeSourceFile, context.deckName);
    setLabelText(dialog, Control::MergeDestinationFile, context.deckName);
    populateListWidget(dialog, Control::MappingSourceList, context.fieldNames);
    populateListWidget(dialog, Control::MappingDestinationList, context.fieldNames);
    setLabelText(dialog, Control::MappingStatus, QObject::tr("Map source data boxes to destination data boxes."));
}

void initializeExportDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    createExclusiveButtonGroup(dialog, {Control::AllCards, Control::SelectedCards}, Control::AllCards);
    populateListWidget(dialog, Control::MappingSourceList, context.fieldNames);
    populateListWidget(dialog, Control::MappingDestinationList, {});
    setupListTransfer(
        dialog,
        Control::MappingSourceList,
        Control::MappingDestinationList,
        Control::MappingAdd,
        Control::MappingAddAll,
        Control::MappingRemove,
        Control::MappingRemoveAll);
    setLabelText(dialog, Control::ExportSourceFile, context.deckName);
    setLabelText(dialog, Control::ExportFormat, QObject::tr("Text export"));
}

void initializeDesignReportsDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    auto* reportList = findUiControl<QListWidget>(dialog, Control::ReportsList);
    auto* printButton = findUiControl<QAbstractButton>(dialog, Control::Ok);
    auto* deleteButton = findUiControl<QAbstractButton>(dialog, Control::ReportsDelete);
    auto* modifyButton = findUiControl<QAbstractButton>(dialog, Control::ReportsModify);
    auto* undoButton = findUiControl<QAbstractButton>(dialog, Control::ReportsUndoDelete);
    auto* addDefaultsButton = findUiControl<QAbstractButton>(dialog, Control::ReportsAddDefaults);

    if (reportList != nullptr) {
        reportList->clear();
        const int typeWidth = reportList->fontMetrics().horizontalAdvance(QObject::tr("Report")) + 18;
        const QVector<UiBuilder::DialogContext::ReportListEntry> reports = context.reports.isEmpty()
            ? [&context]() {
                  QVector<UiBuilder::DialogContext::ReportListEntry> fallback;
                  for (const QString& name : context.reportNames) {
                      fallback.append({QObject::tr("Report"), name});
                  }
                  return fallback;
              }()
            : context.reports;

        for (const auto& report : reports) {
            auto* item = new QListWidgetItem(report.description, reportList);
            item->setSizeHint(QSize(0, reportList->fontMetrics().height() + 8));
            auto* row = new QWidget(reportList);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 0, 4, 0);
            rowLayout->setSpacing(8);
            auto* typeLabel = new QLabel(report.type, row);
            typeLabel->setObjectName(QStringLiteral("reportType"));
            typeLabel->setFixedWidth(typeWidth);
            auto* descriptionLabel = new QLabel(report.description, row);
            descriptionLabel->setObjectName(QStringLiteral("reportDescription"));
            rowLayout->addWidget(typeLabel);
            rowLayout->addWidget(descriptionLabel, 1);
            reportList->setItemWidget(item, row);
        }
    }

    auto updateButtons = [reportList, printButton, deleteButton, modifyButton]() {
        const bool hasSelection = reportList != nullptr && reportList->currentRow() >= 0;
        if (printButton != nullptr) {
            printButton->setEnabled(hasSelection);
        }
        if (deleteButton != nullptr) {
            deleteButton->setEnabled(hasSelection);
        }
        if (modifyButton != nullptr) {
            modifyButton->setEnabled(hasSelection);
        }
    };

    if (reportList != nullptr) {
        QObject::connect(reportList, &QListWidget::currentRowChanged, dialog, [updateButtons](int) {
            updateButtons();
        });
        QObject::connect(reportList, &QListWidget::itemDoubleClicked, dialog, [dialog](QListWidgetItem*) {
            dialog->accept();
        });
    }
    if (undoButton != nullptr) {
        undoButton->setEnabled(false);
    }
    if (addDefaultsButton != nullptr) {
        const bool alreadyHasDefaults =
            context.reportNames.contains(QObject::tr("Default Page Report")) &&
            context.reportNames.contains(QObject::tr("Default Row Report"));
        addDefaultsButton->setEnabled(!alreadyHasDefaults);
    }
    updateButtons();
}

void initializeSaveDesignDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    populateListWidget(dialog, Control::SaveDesignList, context.reportNames);
    auto* nameEdit = findUiControl<QLineEdit>(dialog, Control::SaveDesignName);
    auto* okButton = findUiControl<QAbstractButton>(dialog, Control::Ok);
    auto* reportList = findUiControl<QListWidget>(dialog, Control::SaveDesignList);
    if (nameEdit != nullptr && okButton != nullptr) {
        okButton->setEnabled(false);
        QObject::connect(nameEdit, &QLineEdit::textChanged, dialog, [okButton](const QString& text) {
            okButton->setEnabled(!text.trimmed().isEmpty());
        });
    }
    if (nameEdit != nullptr && reportList != nullptr) {
        auto copySelectedName = [nameEdit](QListWidgetItem* item) {
            if (item != nullptr) {
                nameEdit->setText(item->text());
            }
        };
        QObject::connect(reportList, &QListWidget::currentItemChanged, dialog,
                         [copySelectedName](QListWidgetItem* current, QListWidgetItem*) {
            copySelectedName(current);
        });
        QObject::connect(reportList, &QListWidget::itemDoubleClicked, dialog,
                         [copySelectedName](QListWidgetItem* item) {
            copySelectedName(item);
        });
    }
}

void initializeColorDialog(QDialog* dialog)
{
    QStringList roles;
    for (int stringId = StringId::ColorRoleFirst; stringId <= StringId::ColorRoleLast; ++stringId) {
        roles.append(legacyString(stringId));
    }
    populateComboBox(dialog, Control::ColorRoleCombo, roles, false);
    auto* roleCombo = findUiControl<QComboBox>(dialog, Control::ColorRoleCombo);
    if (roleCombo != nullptr && roleCombo->count() > 5) {
        roleCombo->setCurrentIndex(5);
    }

    auto* swatchGrid = dynamic_cast<ColorSwatchGrid*>(findUiControl<QWidget>(dialog, Control::ColorCustomGrid));
    if (swatchGrid != nullptr) {
        const QRect oldGeometry = swatchGrid->geometry();
        const int desiredWidth = std::max(oldGeometry.width(), 360);
        const int desiredHeight = std::max(oldGeometry.height(), 210);
        const int heightDelta = desiredHeight - oldGeometry.height();
        swatchGrid->setGeometry(oldGeometry.x(), oldGeometry.y(), desiredWidth, desiredHeight);
        if (heightDelta > 0) {
            const QList<QWidget*> controls =
                dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget* control : controls) {
                if (control != swatchGrid && control->y() >= oldGeometry.bottom()) {
                    control->move(control->x(), control->y() + heightDelta);
                }
            }
            dialog->resize(std::max(dialog->width(), swatchGrid->geometry().right() + 16),
                           dialog->height() + heightDelta);
        }
        swatchGrid->setSelectedRole(roleCombo != nullptr ? roleCombo->currentIndex() : 5);
    }

    if (roleCombo != nullptr && swatchGrid != nullptr) {
        QObject::connect(roleCombo, &QComboBox::currentIndexChanged, dialog, [swatchGrid](int index) {
            swatchGrid->setSelectedRole(index);
        });
    }

    if (auto* useSystem = findUiControl<QAbstractButton>(dialog, Control::ColorUseSystem)) {
        useSystem->setText(QObject::tr("Use system colors"));
        useSystem->setCheckable(true);
        useSystem->setVisible(true);
        QObject::connect(useSystem, &QAbstractButton::toggled, dialog, [swatchGrid](bool checked) {
            if (swatchGrid != nullptr) {
                swatchGrid->setUseSystemColors(checked);
            }
        });
    }
}

void initializeAddSystemBoxDialog(QDialog* dialog)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::SystemBoxDateCategory, Control::SystemBoxNumberCategory, Control::SystemBoxSystemCategory},
        Control::SystemBoxDateCategory);
    createExclusiveButtonGroup(
        dialog,
        {Control::SystemBoxLeft, Control::SystemBoxCenter, Control::SystemBoxRight},
        Control::SystemBoxLeft);

    QStringList dates;
    for (int stringId = StringId::SystemBoxDateFormatFirst; stringId <= StringId::SystemBoxDateFormatLast; ++stringId) {
        dates.append(legacyString(stringId));
    }
    QStringList numbers;
    for (int stringId = StringId::SystemBoxNumberFormatFirst; stringId <= StringId::SystemBoxNumberFormatLast; ++stringId) {
        numbers.append(legacyString(stringId));
    }
    QStringList system;
    for (int stringId = StringId::SystemBoxFieldFirst; stringId <= StringId::SystemBoxFieldLast; ++stringId) {
        system.append(legacyString(stringId));
    }
    populateComboBox(dialog, Control::SystemBoxDateFormats, dates, false);
    populateComboBox(dialog, Control::SystemBoxNumberFormats, numbers, false);
    populateComboBox(dialog, Control::SystemBoxSystemFields, system, false);

    auto updatePreview = [dialog]() {
        auto* preview = dynamic_cast<PicturePreview*>(findUiControl<QWidget>(dialog, Control::SystemBoxPreview));
        if (preview == nullptr) {
            return;
        }

        int tokenId = StringId::SystemBoxDateTokenFirst;
        if (const auto* number = findUiControl<QAbstractButton>(dialog, Control::SystemBoxNumberCategory);
            number != nullptr && number->isChecked()) {
            const auto* combo = findUiControl<QComboBox>(dialog, Control::SystemBoxNumberFormats);
            tokenId = StringId::SystemBoxNumberTokenFirst +
                (combo == nullptr ? 0 : std::max(0, combo->currentIndex()));
        } else if (const auto* system = findUiControl<QAbstractButton>(dialog, Control::SystemBoxSystemCategory);
                   system != nullptr && system->isChecked()) {
            const auto* combo = findUiControl<QComboBox>(dialog, Control::SystemBoxSystemFields);
            tokenId = StringId::SystemBoxFieldTokenFirst +
                (combo == nullptr ? 0 : std::max(0, combo->currentIndex()));
        } else {
            const auto* combo = findUiControl<QComboBox>(dialog, Control::SystemBoxDateFormats);
            tokenId = StringId::SystemBoxDateTokenFirst +
                (combo == nullptr ? 0 : std::max(0, combo->currentIndex()));
        }

        Qt::Alignment alignment = Qt::AlignLeft;
        if (const auto* center = findUiControl<QAbstractButton>(dialog, Control::SystemBoxCenter);
            center != nullptr && center->isChecked()) {
            alignment = Qt::AlignHCenter;
        } else if (const auto* right = findUiControl<QAbstractButton>(dialog, Control::SystemBoxRight);
                   right != nullptr && right->isChecked()) {
            alignment = Qt::AlignRight;
        }

        preview->setSampleText(legacyString(tokenId));
        preview->setSampleAlignment(alignment);
        preview->setSampleStyle(
            findUiControl<QAbstractButton>(dialog, Control::SystemBoxBold) != nullptr &&
                findUiControl<QAbstractButton>(dialog, Control::SystemBoxBold)->isChecked(),
            findUiControl<QAbstractButton>(dialog, Control::SystemBoxItalic) != nullptr &&
                findUiControl<QAbstractButton>(dialog, Control::SystemBoxItalic)->isChecked(),
            findUiControl<QAbstractButton>(dialog, Control::SystemBoxUnderline) != nullptr &&
                findUiControl<QAbstractButton>(dialog, Control::SystemBoxUnderline)->isChecked());
    };

    const QList<int> previewButtons = {
        Control::SystemBoxDateCategory,
        Control::SystemBoxNumberCategory,
        Control::SystemBoxSystemCategory,
        Control::SystemBoxLeft,
        Control::SystemBoxCenter,
        Control::SystemBoxRight,
        Control::SystemBoxBold,
        Control::SystemBoxItalic,
        Control::SystemBoxUnderline,
    };
    for (int controlId : previewButtons) {
        if (auto* button = findUiControl<QAbstractButton>(dialog, controlId)) {
            QObject::connect(button, &QAbstractButton::toggled, dialog, [updatePreview](bool) {
                updatePreview();
            });
        }
    }
    const auto bindFormatCombo = [dialog, updatePreview](int comboId, int categoryId) {
        auto* combo = findUiControl<QComboBox>(dialog, comboId);
        if (combo == nullptr) {
            return;
        }
        QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [dialog, categoryId, updatePreview](int) {
            auto* category = findUiControl<QAbstractButton>(dialog, categoryId);
            if (category != nullptr && !category->isChecked()) {
                category->setChecked(true);
                return;
            }
            updatePreview();
        });
    };
    bindFormatCombo(Control::SystemBoxDateFormats, Control::SystemBoxDateCategory);
    bindFormatCombo(Control::SystemBoxNumberFormats, Control::SystemBoxNumberCategory);
    bindFormatCombo(Control::SystemBoxSystemFields, Control::SystemBoxSystemCategory);
    updatePreview();
}

void initializeDeckDescriptionDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    setLabelText(dialog, Control::DeckDescriptionFileName, context.deckName);
    if (auto* description = findUiControl<QLineEdit>(dialog, Control::DeckDescriptionText)) {
        description->setText(context.deckDescription);
    }
}

void initializeSecurityDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    setLabelText(dialog, Control::SecurityFileName, context.deckName);
    auto* password = findUiControl<QLineEdit>(dialog, Control::SecurityPassword);
    if (password == nullptr) {
        return;
    }

    password->setMaxLength(8);
    password->setEchoMode(QLineEdit::Password);
    QObject::connect(password, &QLineEdit::textChanged, dialog, [password](const QString& text) {
        const QString upper = text.left(8).toUpper();
        if (text == upper) {
            return;
        }

        const QSignalBlocker blocker(password);
        password->setText(upper);
        password->setCursorPosition(upper.size());
    });
}

void initializePhoneDialog(QDialog* dialog)
{
    populateListWidget(dialog, Control::PhoneCardNumbers, {QObject::tr("Current card phone fields")});
    populateListWidget(dialog, Control::PhoneQuickDials, {QObject::tr("Operator"), QObject::tr("Information")});
    if (auto* lineEdit = findUiControl<QLineEdit>(dialog, Control::PhoneNumber)) {
        lineEdit->setInputMask(QStringLiteral(""));
    }
    if (auto* placeCall = findUiControl<QAbstractButton>(dialog, Control::PhonePlaceCall)) {
        QObject::connect(placeCall, &QAbstractButton::clicked, dialog, &QDialog::accept);
    }
}

void initializePhoneDefinitionDialog(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    constexpr int obsoleteSectionBottomPx = 66;
    constexpr int usefulSectionShiftPx = 62;
    for (QWidget* widget : controls) {
        const int controlId = widget->property("originalControlId").toInt();
        if (controlId == Control::Ok || controlId == Control::Cancel || controlId == Control::Help) {
            continue;
        }
        QRect rect = widget->geometry();
        if (rect.top() < obsoleteSectionBottomPx && rect.left() < dialog->width() - 80) {
            widget->hide();
            continue;
        }
        if (rect.top() >= obsoleteSectionBottomPx) {
            rect.translate(0, -usefulSectionShiftPx);
            widget->setGeometry(rect);
        }
    }
    dialog->resize(dialog->width(), std::max(1, dialog->height() - usefulSectionShiftPx));
    dialog->setMinimumSize(dialog->size());

    auto* quickDials = findUiControl<QListWidget>(dialog, Control::PhoneQuickDials);
    if (quickDials == nullptr) {
        return;
    }
    const QPointer<QListWidget> guardedQuickDials(quickDials);
    const QPointer<QAbstractButton> addButton(findUiControl<QAbstractButton>(dialog, Control::PhoneQuickDialAdd));
    const QPointer<QAbstractButton> modifyButton(findUiControl<QAbstractButton>(dialog, Control::PhoneQuickDialModify));
    const QPointer<QAbstractButton> deleteButton(findUiControl<QAbstractButton>(dialog, Control::PhoneQuickDialDelete));
    const QPointer<QAbstractButton> okButton(findUiControl<QAbstractButton>(dialog, Control::Ok));
    const auto updateQuickDialButtons = [guardedQuickDials, addButton, modifyButton, deleteButton, okButton]() {
        if (guardedQuickDials.isNull()) {
            return;
        }
        const int count = guardedQuickDials->count();
        const auto setEnabled = [okButton](const QPointer<QAbstractButton>& button, bool enabled) {
            if (button.isNull()) {
                return;
            }
            if (!enabled && button->hasFocus()) {
                if (!okButton.isNull()) {
                    okButton->setFocus();
                }
            }
            button->setEnabled(enabled);
        };
        setEnabled(addButton, count < 100);
        setEnabled(modifyButton, count > 0);
        setEnabled(deleteButton, count > 0);
    };
    QObject::connect(quickDials->model(), &QAbstractItemModel::rowsInserted, dialog, updateQuickDialButtons);
    QObject::connect(quickDials->model(), &QAbstractItemModel::rowsRemoved, dialog, updateQuickDialButtons);
    QObject::connect(quickDials->model(), &QAbstractItemModel::modelReset, dialog, updateQuickDialButtons);
    updateQuickDialButtons();
}

void initializeQuickDialDialog(QDialog* dialog)
{
    auto* number = findUiControl<QLineEdit>(dialog, Control::QuickDialNumber);
    auto* ok = findUiControl<QAbstractButton>(dialog, Control::Ok);
    if (number == nullptr || ok == nullptr) {
        return;
    }

    const auto updateAcceptState = [dialog, number, ok]() {
        const bool enabled = !number->text().isEmpty();
        if (!enabled && ok->hasFocus()) {
            if (auto* cancel = findUiControl<QAbstractButton>(dialog, Control::Cancel)) {
                cancel->setFocus();
            }
        }
        ok->setEnabled(enabled);
    };
    QObject::connect(number, &QLineEdit::textChanged, dialog, updateAcceptState);
    updateAcceptState();
}

void initializeReportFormDialog(QDialog* dialog)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::ReportFormCard, Control::ReportFormLabel, Control::ReportFormReport},
        Control::ReportFormReport);
    if (auto* formList = findUiControl<QListWidget>(dialog, Control::ReportFormList)) {
        QObject::connect(formList, &QListWidget::itemDoubleClicked, dialog, [dialog](QListWidgetItem*) {
            dialog->accept();
        });
    }
}

void initializeDefineFormDialog(QDialog* dialog)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::DefineFormCard, Control::DefineFormLabel, Control::DefineFormReport},
        Control::DefineFormReport);
    createExclusiveButtonGroup(
        dialog,
        {Control::DefineFormPortrait, Control::DefineFormLandscape},
        Control::DefineFormPortrait);
}

void initializeTextFrameDialog(QDialog* dialog)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::FrameAlignmentLeft, Control::FrameAlignmentCenter, Control::FrameAlignmentRight},
        Control::FrameAlignmentLeft);
}

void initializeDataFrameDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    populateComboBox(dialog, Control::DataFrameFieldList, context.fieldNames, false);
    createExclusiveButtonGroup(
        dialog,
        {Control::FrameAlignmentLeft, Control::FrameAlignmentCenter, Control::FrameAlignmentRight},
        Control::FrameAlignmentLeft);
}

void initializeUiDialog(
    QDialog* dialog,
    const QString& dialogName,
    const UiBuilder::DialogContext& context)
{
    if (dialogName == QStringLiteral("NEWFILE")) {
        initializeNewFileDialog(dialog, context);
    } else if (dialogName == QStringLiteral("SEARCH")) {
        initializeSearchDialog(dialog, context);
    } else if (dialogName == QStringLiteral("REPLACE")) {
        initializeReplaceDialog(dialog, context);
    } else if (dialogName == QStringLiteral("PRINT")) {
        initializePrintDialog(dialog, context);
    } else if (dialogName == QStringLiteral("SORT")) {
        initializeSortDialog(dialog, context);
    } else if (dialogName == QStringLiteral("MERGEDLG")) {
        initializeMergeDialog(dialog, context);
    } else if (dialogName == QStringLiteral("EXPORT")) {
        initializeExportDialog(dialog, context);
    } else if (dialogName == QStringLiteral("DESIGNREPORTS")) {
        initializeDesignReportsDialog(dialog, context);
    } else if (dialogName == QStringLiteral("SAVEDESIGN")) {
        initializeSaveDesignDialog(dialog, context);
    } else if (dialogName == QStringLiteral("CHOOSECOLOR")) {
        initializeColorDialog(dialog);
    } else if (dialogName == QStringLiteral("ADDSYSTEMBOX")) {
        initializeAddSystemBoxDialog(dialog);
    } else if (dialogName == QStringLiteral("CHANGEDESC")) {
        initializeDeckDescriptionDialog(dialog, context);
    } else if (
        dialogName == QStringLiteral("ADDSECURITY") ||
        dialogName == QStringLiteral("REMOVESECURITY") ||
        dialogName == QStringLiteral("GETPASSWORD") ||
        dialogName == QStringLiteral("VERIFYPASSWORD")) {
        initializeSecurityDialog(dialog, context);
    } else if (dialogName == QStringLiteral("CALL")) {
        initializePhoneDialog(dialog);
    } else if (dialogName == QStringLiteral("PHNDEF")) {
        initializePhoneDefinitionDialog(dialog);
    } else if (dialogName == QStringLiteral("QUICKDIAL")) {
        initializeQuickDialDialog(dialog);
    } else if (dialogName == QStringLiteral("REPORTFORM")) {
        initializeReportFormDialog(dialog);
    } else if (dialogName == QStringLiteral("DEFINEFORM")) {
        initializeDefineFormDialog(dialog);
    } else if (dialogName == QStringLiteral("LINEFRAME")) {
        initializeLineFrameDialog(dialog);
    } else if (dialogName == QStringLiteral("DATAFRAME")) {
        initializeDataFrameDialog(dialog, context);
    } else if (dialogName == QStringLiteral("TEXTFRAME") || dialogName == QStringLiteral("TPLTEXTFRAME")) {
        initializeTextFrameDialog(dialog);
    }
}

} // namespace

bool UiBuilder::populateMenuBar(
    QMenuBar* menuBar,
    int menuId,
    QObject* actionParent,
    const std::function<void(QAction*)>& configureAction)
{
    enableLegacyMnemonics();
    if (menuBar == nullptr) {
        return false;
    }

    const UiResourceData::UiMenu* menu = findMenu(menuId);
    if (menu == nullptr || menu->items == nullptr || menu->itemCount == 0) {
        return false;
    }

    menuBar->clear();
    for (std::size_t index = 0; index < menu->itemCount; ++index) {
        const UiResourceData::UiMenuItem& item = menu->items[index];
        QKeySequence shortcut;
        const QString text = displayTextAndShortcut(uiText(item.text), &shortcut);
        if (item.children != nullptr && item.childCount > 0) {
            QMenu* topMenu = menuBar->addMenu(text);
            populateMenu(topMenu, item.children, item.childCount, actionParent, configureAction);
        } else if (item.id != 0) {
            QAction* action = new QAction(text, actionParent);
            action->setData(item.id);
            if (!shortcut.isEmpty()) {
                action->setShortcut(shortcut);
            }
            if (configureAction) {
                configureAction(action);
            }
            menuBar->addAction(action);
        }
    }

    return true;
}

std::unique_ptr<QDialog> UiBuilder::createDialog(
    const QString& dialogName,
    QWidget* parent,
    const DialogContext& context)
{
    enableLegacyMnemonics();
    const UiResourceData::UiDialog* dialogResource = findDialog(dialogName);
    if (dialogResource == nullptr || dialogResource->controls == nullptr || dialogResource->controlCount == 0) {
        return {};
    }

    auto dialog = std::make_unique<QDialog>(parent);
    dialog->setProperty("legacyDialogName", dialogName);
    dialog->setWindowTitle(plainVisibleText(uiText(dialogResource->title).isEmpty() ? dialogName : uiText(dialogResource->title)));

    if (dialogResource->fontTypeface != nullptr && dialogResource->fontTypeface[0] != '\0') {
        QString family = uiText(dialogResource->fontTypeface);
        if (family.compare(QStringLiteral("Helv"), Qt::CaseInsensitive) == 0) {
            family = QStringLiteral("Arial");
        }
        dialog->setFont(QFont(family, dialogResource->fontPointSize <= 0 ? 8 : dialogResource->fontPointSize));
    }

    const QRect dialogRect = dialogUnitsToPixels(
        dialog.get(),
        dialogResource->x,
        dialogResource->y,
        dialogResource->width,
        dialogResource->height);
    dialog->resize(dialogRect.width(), dialogRect.height());
    dialog->setMinimumSize(dialogRect.width(), dialogRect.height());

    QRect childrenBounds;
    for (std::size_t index = 0; index < dialogResource->controlCount; ++index) {
        const UiResourceData::UiControl& control = dialogResource->controls[index];
        QWidget* widget = createControl(dialog.get(), control);
        const QRect geometry = normalizedControlGeometry(widget, control);
        widget->setGeometry(geometry);
        childrenBounds = childrenBounds.united(geometry);
        connectStandardDialogButtons(dialog.get(), widget, control.id);
    }

    if (childrenBounds.isValid()) {
        const int margin = 16;
        const QSize expandedSize(
            std::max(dialog->width(), childrenBounds.right() + margin),
            std::max(dialog->height(), childrenBounds.bottom() + margin));
        dialog->resize(expandedSize);
        dialog->setMinimumSize(expandedSize);
    }

    initializeUiDialog(dialog.get(), dialogName, context);
    refineDialogGeometry(dialog.get());
    if (dialogName == QStringLiteral("SEARCH")) {
        resizeDialogToVisibleContent(dialog.get());
    }
    bindDialogLabelMnemonics(dialog.get());

    return dialog;
}

void UiBuilder::setColorDialogState(QDialog* dialog, const QStringList& colors, bool useSystemColors)
{
    if (dialog == nullptr) {
        return;
    }
    if (auto* grid = dynamic_cast<ColorSwatchGrid*>(controlById(dialog, Control::ColorCustomGrid))) {
        grid->setCustomColors(colors);
        grid->setUseSystemColors(useSystemColors);
    }
    if (auto* button = qobject_cast<QAbstractButton*>(controlById(dialog, Control::ColorUseSystem))) {
        button->setChecked(useSystemColors);
    }
}

QStringList UiBuilder::colorDialogColors(const QDialog* dialog)
{
    if (dialog == nullptr) {
        return {};
    }
    if (auto* grid = dynamic_cast<ColorSwatchGrid*>(controlById(const_cast<QDialog*>(dialog), Control::ColorCustomGrid))) {
        return grid->customColors();
    }
    return {};
}

bool UiBuilder::colorDialogUsesSystemColors(const QDialog* dialog)
{
    if (dialog == nullptr) {
        return true;
    }
    if (auto* button = qobject_cast<QAbstractButton*>(controlById(const_cast<QDialog*>(dialog), Control::ColorUseSystem))) {
        return button->isChecked();
    }
    return true;
}

QStringList UiBuilder::dialogNames()
{
    QStringList names;
    const UiResourceData::UiDialog* dialogs = UiResourceData::dialogs();
    const std::size_t count = UiResourceData::dialogCount();
    for (std::size_t index = 0; index < count; ++index) {
        names.append(uiText(dialogs[index].name));
    }
    return names;
}

QWidget* UiBuilder::controlById(QWidget* parent, int controlId)
{
    return findUiControl<QWidget>(parent, controlId);
}

} // namespace CardStack
