#include "UiBuilder.h"

#include "UiIds.h"
#include "UiResourceData.h"

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
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector>

#include <algorithm>

namespace CardStack {
namespace {

namespace Control = UiIds::Control;
namespace StringId = UiIds::StringId;

constexpr int NarrowStaticDluWidth = 6;
constexpr int ComboMinimumContentsLength = 10;
constexpr int ComboDropDownArrowPaddingPx = 56;
constexpr int EditableComboMinimumWidthPx = 150;
constexpr int FixedComboMinimumWidthPx = 112;
constexpr int EditableComboMaximumWidthPx = 280;
constexpr int FixedComboMaximumWidthPx = 190;
constexpr int DialogControlGapPx = 8;
constexpr int DialogOuterMarginPx = 16;
constexpr int ComboMinimumShrunkWidthPx = 92;
constexpr int TextMinimumShrunkWidthPx = 80;
constexpr int HtmlDialogWidthPx = 760;
constexpr int HtmlDialogHeightPx = 620;

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
            "<p>CardStack uses the operating system phone-link handler instead of modem configuration.</p>"
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
            *shortcut = QKeySequence(shortcutText);
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

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().shadow().color());
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const int margin = 8;
        const int gap = 6;
        const int swatchWidth = std::max(14, (width() - margin * 2 - gap * 2) / 3);
        const int swatchHeight = std::max(14, (height() - margin * 2 - gap * 2) / 3);

        for (int index = 0; index < m_customColors.size(); ++index) {
            const int row = index / 3;
            const int column = index % 3;
            const QRect swatch(
                margin + column * (swatchWidth + gap),
                margin + row * (swatchHeight + gap),
                swatchWidth,
                swatchHeight);

            painter.fillRect(swatch, colorForRole(index));
            painter.setPen(index == m_selectedRole ? palette().highlight().color() : palette().mid().color());
            painter.drawRect(swatch.adjusted(0, 0, -1, -1));
            if (index == m_selectedRole) {
                painter.drawRect(swatch.adjusted(2, 2, -3, -3));
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const int role = roleAt(event->position().toPoint());
        if (role < 0) {
            return;
        }

        setSelectedRole(role);
        if (m_useSystemColors) {
            return;
        }

        const QColor color = QColorDialog::getColor(m_customColors.at(role), this, tr("CardStack Color"));
        if (color.isValid()) {
            m_customColors[role] = color;
            update();
        }
    }

private:
    QColor colorForRole(int role) const
    {
        if (!m_useSystemColors) {
            return m_customColors.at(role);
        }

        const QList<QPalette::ColorRole> systemRoles = {
            QPalette::WindowText,
            QPalette::Text,
            QPalette::ButtonText,
            QPalette::ToolTipText,
            QPalette::Base,
            QPalette::AlternateBase,
            QPalette::Window,
        };
        return palette().color(systemRoles.at(std::clamp(role, 0, static_cast<int>(systemRoles.size()) - 1)));
    }

    int roleAt(const QPoint& point) const
    {
        const int margin = 8;
        const int gap = 6;
        const int swatchWidth = std::max(14, (width() - margin * 2 - gap * 2) / 3);
        const int swatchHeight = std::max(14, (height() - margin * 2 - gap * 2) / 3);

        for (int index = 0; index < m_customColors.size(); ++index) {
            const int row = index / 3;
            const int column = index % 3;
            const QRect swatch(
                margin + column * (swatchWidth + gap),
                margin + row * (swatchHeight + gap),
                swatchWidth,
                swatchHeight);
            if (swatch.contains(point)) {
                return index;
            }
        }
        return -1;
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

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().shadow().color());
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

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

    if (className.compare(QStringLiteral("BTN_pict"), Qt::CaseInsensitive) == 0) {
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
        if (text.isEmpty() && control.width <= NarrowStaticDluWidth) {
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
    widget->setToolTip(QStringLiteral("%1: %2").arg(id).arg(className));
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
        preferredWidth = std::max(preferredWidth, EditableComboMinimumWidthPx);
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

void refineDefineFormDialog(QDialog* dialog)
{
    if (dialog->property("legacyDialogName").toString() != QStringLiteral("DEFINEFORM")) {
        return;
    }

    constexpr int NumericFieldWidthPx = 48;
    constexpr int CountFieldWidthPx = 36;
    constexpr int MicroScrollWidthPx = 10;
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
}

void refineDialogGeometry(QDialog* dialog)
{
    const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);

    for (QWidget* widget : controls) {
        if (!isTextSizedControl(widget)) {
            continue;
        }

        QRect rect = widget->geometry();
        rect.setHeight(std::max(rect.height(), widget->sizeHint().height()));
        int preferredWidth = preferredTextControlWidth(widget);
        if (widget->maximumWidth() < QWIDGETSIZE_MAX) {
            preferredWidth = std::min(preferredWidth, widget->maximumWidth());
        }
        rect.setWidth(std::max(rect.width(), preferredWidth));
        if (widget->maximumWidth() < QWIDGETSIZE_MAX) {
            rect.setWidth(std::min(rect.width(), widget->maximumWidth()));
        }
        widget->setGeometry(rect);
    }

    for (QWidget* widget : controls) {
        if (!isTextSizedControl(widget)) {
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

    refineDefineFormDialog(dialog);

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
    for (T* control : controls) {
        if (control->property("originalControlId").toInt() == controlId) {
            return control;
        }
    }

    return nullptr;
}

void setControlsEnabled(QWidget* parent, const QList<int>& controlIds, bool enabled)
{
    for (int controlId : controlIds) {
        if (QWidget* control = findUiControl<QWidget>(parent, controlId)) {
            control->setEnabled(enabled);
        }
    }
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

    auto updateTemplateState = [sourceCombo, templateList]() {
        const int selected = sourceCombo->currentIndex();
        templateList->setEnabled(selected == 0 || selected == 2);
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
    setControlWidth(dialog, Control::SearchType, 136);
    setControlWidth(dialog, Control::SearchSecondType, 136);

    createExclusiveButtonGroup(
        dialog,
        {Control::SearchCompareNone, Control::SearchCompareAnd, Control::SearchCompareOr},
        Control::SearchCompareNone);
    createExclusiveButtonGroup(
        dialog,
        {Control::SearchDirectionBeginning, Control::SearchDirectionForward, Control::SearchDirectionBackward},
        Control::SearchDirectionBeginning);

    const QList<int> secondClauseControls = {
        Control::SearchSecondText,
        Control::SearchSecondAllDataBoxes,
        Control::SearchSecondType,
        Control::SearchSecondWholeWord,
        Control::SearchSecondCaseSensitive,
        Control::SearchSecondSoundsLike,
    };
    setControlsEnabled(dialog, secondClauseControls, false);

    auto updateSecondClause = [dialog, secondClauseControls]() {
        const auto* andButton = findUiControl<QAbstractButton>(dialog, Control::SearchCompareAnd);
        const auto* orButton = findUiControl<QAbstractButton>(dialog, Control::SearchCompareOr);
        const bool enabled = (andButton != nullptr && andButton->isChecked()) ||
            (orButton != nullptr && orButton->isChecked());
        setControlsEnabled(dialog, secondClauseControls, enabled);
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
}

void initializePrintDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    createExclusiveButtonGroup(
        dialog,
        {Control::PrintThisCard, Control::PrintAllCards, Control::PrintSelectedCards},
        Control::PrintThisCard);

    if (auto* copyCount = findUiControl<QLineEdit>(dialog, Control::PrintCopyCount)) {
        copyCount->setText(QStringLiteral("1"));
    }
    setLabelText(dialog, Control::PrintPrinterName, QObject::tr("Default printer"));
    setLabelText(
        dialog,
        Control::PrintReportName,
        context.reportNames.isEmpty() ? QObject::tr("Default Page Report") : context.reportNames.first());
    setLabelText(dialog, Control::PrintSummary1, QObject::tr("All selected records are eligible for printing."));
    setLabelText(dialog, Control::PrintSummary2, QObject::tr("Use Define Search to change the selected-card set."));
    setLabelText(dialog, Control::PrintSummary3, QObject::tr("Print Preview opens the preview dialog."));
}

void initializeSortDialog(QDialog* dialog, const UiBuilder::DialogContext& context)
{
    QStringList sortFields;
    sortFields.append(QString());
    sortFields.append(context.fieldNames);
    populateComboBox(dialog, Control::SortFieldLevel1, sortFields, false);
    populateComboBox(dialog, Control::SortFieldLevel2, sortFields, false);
    populateComboBox(dialog, Control::SortFieldLevel3, sortFields, false);
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
    populateListWidget(dialog, Control::ReportsList, context.reportNames);
    auto* reportList = findUiControl<QListWidget>(dialog, Control::ReportsList);
    auto* printButton = findUiControl<QAbstractButton>(dialog, Control::Ok);
    auto* deleteButton = findUiControl<QAbstractButton>(dialog, Control::ReportsDelete);
    auto* modifyButton = findUiControl<QAbstractButton>(dialog, Control::ReportsModify);
    auto* undoButton = findUiControl<QAbstractButton>(dialog, Control::ReportsUndoDelete);
    auto* addDefaultsButton = findUiControl<QAbstractButton>(dialog, Control::ReportsAddDefaults);

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
        QObject::connect(reportList, &QListWidget::itemClicked, dialog, [nameEdit](QListWidgetItem* item) {
            if (item != nullptr) {
                nameEdit->setText(item->text());
            }
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
        swatchGrid->setSelectedRole(roleCombo != nullptr ? roleCombo->currentIndex() : 5);
    }

    if (roleCombo != nullptr && swatchGrid != nullptr) {
        QObject::connect(roleCombo, &QComboBox::currentIndexChanged, dialog, [swatchGrid](int index) {
            swatchGrid->setSelectedRole(index);
        });
    }

    if (auto* useSystem = findUiControl<QAbstractButton>(dialog, Control::ColorUseSystem)) {
        useSystem->setText(QObject::tr("Use system palette colors"));
        useSystem->setCheckable(true);
#ifndef Q_OS_WIN
        useSystem->setVisible(false);
#endif
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
    for (int controlId : {Control::SystemBoxDateFormats, Control::SystemBoxNumberFormats, Control::SystemBoxSystemFields}) {
        if (auto* combo = findUiControl<QComboBox>(dialog, controlId)) {
            QObject::connect(combo, &QComboBox::currentIndexChanged, dialog, [updatePreview](int) {
                updatePreview();
            });
        }
    }
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
    }
}

} // namespace

bool UiBuilder::populateMenuBar(
    QMenuBar* menuBar,
    int menuId,
    QObject* actionParent,
    const std::function<void(QAction*)>& configureAction)
{
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

    return dialog;
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
