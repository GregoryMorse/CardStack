#include "UiBuilder.h"
#include "UiResourceData.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QSet>
#include <QSpinBox>
#include <QStyle>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>

#include "UiIds.h"

using namespace CardStack;

namespace {

void collectCommandIdsFromMenu(QMenu* menu, QSet<int>* commandIds)
{
    for (QAction* action : menu->actions()) {
        if (QMenu* submenu = action->menu()) {
            collectCommandIdsFromMenu(submenu, commandIds);
            continue;
        }

        if (action->data().isValid()) {
            commandIds->insert(action->data().toInt());
        }
    }
}


void collectCommandIdsFromResourceItems(
    const UiResourceData::UiMenuItem* items,
    std::size_t itemCount,
    QSet<int>* commandIds)
{
    for (std::size_t index = 0; index < itemCount; ++index) {
        const UiResourceData::UiMenuItem& item = items[index];
        if (item.children != nullptr && item.childCount > 0) {
            collectCommandIdsFromResourceItems(item.children, item.childCount, commandIds);
            continue;
        }
        if (!item.separator && item.id != 0) {
            commandIds->insert(item.id);
        }
    }
}

QSet<int> commandIdsFromMenuBar(QMenuBar* menuBar)
{
    QSet<int> commandIds;
    for (QAction* action : menuBar->actions()) {
        if (QMenu* menu = action->menu()) {
            collectCommandIdsFromMenu(menu, &commandIds);
        } else if (action->data().isValid()) {
            commandIds.insert(action->data().toInt());
        }
    }
    return commandIds;
}


QSet<int> commandIdsFromMenuResource(const UiResourceData::UiMenu& menu)
{
    QSet<int> commandIds;
    collectCommandIdsFromResourceItems(menu.items, menu.itemCount, &commandIds);
    return commandIds;
}

UiBuilder::DialogContext populatedContext()
{
    UiBuilder::DialogContext context;
    context.deckName = QStringLiteral("Automation Deck");
    context.deckDescription = QStringLiteral("Dialog coverage smoke test");
    context.fieldNames = {
        QStringLiteral("Name"),
        QStringLiteral("Phone"),
        QStringLiteral("Notes"),
    };
    context.templateNames = {
        QStringLiteral("Address Book"),
        QStringLiteral("Business Cards"),
    };
    context.reportNames = {
        QStringLiteral("Phone List"),
        QStringLiteral("Mailing Labels"),
    };
    context.recentSearches = {QStringLiteral("alpha")};
    context.recentReplacements = {QStringLiteral("omega")};
    return context;
}

bool closeVisibleHelpDialog(QString* text)
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible() || !dialog->windowTitle().contains(QStringLiteral("Help"))) {
            continue;
        }

        if (auto* browser = dialog->findChild<QTextBrowser*>()) {
            if (text != nullptr) {
                *text = browser->toPlainText();
            }
        }
        dialog->reject();
        return true;
    }

    return false;
}

QString widgetDebugName(const QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("<null>");
    }

    QString text;
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
        text = button->text();
    } else if (const auto* label = qobject_cast<const QLabel*>(widget)) {
        text = label->text();
    } else if (const auto* groupBox = qobject_cast<const QGroupBox*>(widget)) {
        text = groupBox->title();
    }
    return QStringLiteral("id=%1 class=%2 object=%3 text=\"%4\" rect=%5,%6 %7x%8")
        .arg(widget->property("originalControlId").toInt())
        .arg(widget->property("uiControlClass").toString())
        .arg(widget->objectName())
        .arg(text)
        .arg(widget->geometry().x())
        .arg(widget->geometry().y())
        .arg(widget->geometry().width())
        .arg(widget->geometry().height());
}

QString plainVisibleText(QString text)
{
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

bool isContainerDecoration(const QWidget* widget)
{
    const auto* label = qobject_cast<const QLabel*>(widget);
    return qobject_cast<const QGroupBox*>(widget) != nullptr ||
        widget->property("uiControlClass").toString() == QStringLiteral("static") &&
            (widget->geometry().width() <= 4 ||
             (label != nullptr && plainVisibleText(label->text()).trimmed().isEmpty()));
}

bool isEmptyStaticDecoration(const QWidget* widget)
{
    const auto* label = qobject_cast<const QLabel*>(widget);
    return label != nullptr &&
        widget->property("uiControlClass").toString() == QStringLiteral("static") &&
        plainVisibleText(label->text()).trimmed().isEmpty();
}

bool isToolbarResourceName(const QString& dialogName)
{
    return dialogName == QStringLiteral("DECK_NAVIGATION_TOOLBAR") ||
        dialogName == QStringLiteral("TEMPLATE_FIELD_TOOLBAR") ||
        dialogName == QStringLiteral("REPORT_FRAME_TOOLBAR") ||
        dialogName == QStringLiteral("COMPACT_DESIGNER_TOOLBAR");
}

bool isContainedByDecoration(const QWidget* decoration, const QWidget* widget)
{
    if (!isContainerDecoration(decoration) || widget == nullptr) {
        return false;
    }

    const QRect decorationRect = decoration->geometry().adjusted(-2, -2, 2, 2);
    const QPoint center = widget->geometry().center();
    return decorationRect.contains(center);
}

int overlapArea(const QRect& first, const QRect& second)
{
    const QRect overlap = first.intersected(second);
    return overlap.isValid() ? overlap.width() * overlap.height() : 0;
}

bool hasMaterialOverlap(const QWidget* first, const QWidget* second)
{
    if (first == nullptr || second == nullptr || first == second) {
        return false;
    }
    if (first->isHidden() || second->isHidden()) {
        return false;
    }
    if ((isEmptyStaticDecoration(first) && qobject_cast<const QLabel*>(second) != nullptr) ||
        (isEmptyStaticDecoration(second) && qobject_cast<const QLabel*>(first) != nullptr)) {
        return false;
    }
    if (isContainedByDecoration(first, second) || isContainedByDecoration(second, first)) {
        return false;
    }

    const QRect firstRect = first->geometry();
    const QRect secondRect = second->geometry();
    const int area = overlapArea(firstRect, secondRect);
    if (area <= 0) {
        return false;
    }
    const QRect overlapRect = firstRect.intersected(secondRect);
    if ((qobject_cast<const QGroupBox*>(first) != nullptr ||
         qobject_cast<const QGroupBox*>(second) != nullptr) &&
        overlapRect.isValid() &&
        std::min(overlapRect.width(), overlapRect.height()) <= 4) {
        return false;
    }

    const int firstArea = firstRect.width() * firstRect.height();
    const int secondArea = secondRect.width() * secondRect.height();
    const int smallerArea = std::max(1, std::min(firstArea, secondArea));
    return area > std::max(6, smallerArea / 8);
}

bool textFitsWidget(const QWidget* widget)
{
    if (widget == nullptr || widget->isHidden()) {
        return true;
    }

    QString text;
    int padding = 8;
    if (const auto* label = qobject_cast<const QLabel*>(widget)) {
        text = plainVisibleText(label->text());
        if (text.trimmed().isEmpty()) {
            return true;
        }
        const QFontMetrics metrics(label->font());
        if (label->wordWrap()) {
            const QRect wrapped = metrics.boundingRect(
                QRect(0, 0, std::max(1, widget->width()), 10000),
                label->alignment() | Qt::TextWordWrap,
                text);
            return wrapped.height() + 2 <= widget->height();
        }
        return metrics.horizontalAdvance(text) + 4 <= widget->width();
    } else if (const auto* groupBox = qobject_cast<const QGroupBox*>(widget)) {
        text = plainVisibleText(groupBox->title());
        padding = 8;
    } else if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
        text = plainVisibleText(button->text());
        if (text.trimmed().isEmpty()) {
            return true;
        }
        return button->sizeHint().width() <= widget->width();
    } else if (const auto* comboBox = qobject_cast<const QComboBox*>(widget)) {
        if (comboBox->count() == 0) {
            return true;
        }
        if (qEnvironmentVariable("QT_QPA_PLATFORM").compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        int widestText = 0;
        const QFontMetrics metrics(comboBox->font());
        for (int index = 0; index < comboBox->count(); ++index) {
            widestText = std::max(widestText, metrics.horizontalAdvance(comboBox->itemText(index)));
        }
        const int arrowWidth = comboBox->style()->pixelMetric(QStyle::PM_ComboBoxFrameWidth, nullptr, comboBox) * 2 +
            comboBox->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, comboBox);
        return widestText + arrowWidth + 18 <= widget->width();
    } else {
        return true;
    }

    if (text.trimmed().isEmpty()) {
        return true;
    }

    const QFontMetrics metrics(widget->font());
    return metrics.horizontalAdvance(text) + padding <= widget->width();
}

QRect groupTitleCollisionRect(const QGroupBox* groupBox)
{
    if (groupBox == nullptr || groupBox->title().trimmed().isEmpty()) {
        return QRect();
    }

    const QFontMetrics metrics(groupBox->font());
    return QRect(
        groupBox->geometry().left() + 8,
        groupBox->geometry().top(),
        metrics.horizontalAdvance(plainVisibleText(groupBox->title())) + 20,
        metrics.height());
}

int requiredComboPopupWidth(const QComboBox* comboBox)
{
    int width = comboBox->width();
    const QFontMetrics metrics(comboBox->font());
    for (int index = 0; index < comboBox->count(); ++index) {
        width = std::max(width, metrics.horizontalAdvance(comboBox->itemText(index)) + 56);
    }
    return width;
}

int effectiveComboPopupWidth(const QComboBox* comboBox)
{
    const QAbstractItemView* view = comboBox->view();
    return std::max(
        comboBox->width(),
        view == nullptr ? 0 : std::max(view->minimumWidth(), view->sizeHint().width()));
}

int closedComboTextAreaWidth(const QComboBox* comboBox)
{
    QStyleOptionComboBox option;
    option.initFrom(comboBox);
    option.editable = comboBox->isEditable();
    option.currentText = comboBox->currentText();
    return comboBox->style()->subControlRect(
        QStyle::CC_ComboBox,
        &option,
        QStyle::SC_ComboBoxEditField,
        comboBox).width();
}

QString safeImageStem(QString text)
{
    text = text.trimmed();
    QString result;
    result.reserve(text.size());
    for (const QChar character : text) {
        result.append(character.isLetterOrNumber() ? character.toLower() : QLatin1Char('_'));
    }
    while (result.contains(QStringLiteral("__"))) {
        result.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    while (result.startsWith(QLatin1Char('_'))) {
        result.remove(0, 1);
    }
    while (result.endsWith(QLatin1Char('_'))) {
        result.chop(1);
    }
    return result.isEmpty() ? QStringLiteral("dialog") : result;
}

int widestComboItemIndex(const QComboBox* comboBox)
{
    if (comboBox == nullptr || comboBox->count() == 0) {
        return -1;
    }

    int widestIndex = 0;
    int widestWidth = -1;
    const QFontMetrics metrics(comboBox->font());
    for (int index = 0; index < comboBox->count(); ++index) {
        const int width = metrics.horizontalAdvance(comboBox->itemText(index));
        if (width > widestWidth) {
            widestWidth = width;
            widestIndex = index;
        }
    }
    return widestIndex;
}

bool isNumericStressEdit(const QLineEdit* lineEdit)
{
    if (lineEdit == nullptr) {
        return false;
    }

    const QString dialogName = lineEdit->window()->property("legacyDialogName").toString();
    const int controlId = lineEdit->property("originalControlId").toInt();
    const QSet<int> numericControls = {
        UiIds::Control::PrintCopyCount,
        UiIds::Control::DefineFormHeight,
        UiIds::Control::DefineFormWidth,
        UiIds::Control::DefineFormMarginTop,
        UiIds::Control::DefineFormMarginLeft,
        UiIds::Control::DefineFormMarginBottom,
        UiIds::Control::DefineFormMarginRight,
        UiIds::Control::DefineFormHorizontalGutter,
        UiIds::Control::DefineFormVerticalGutter,
        UiIds::Control::DefineFormColumns,
        UiIds::Control::DefineFormRows,
        UiIds::Control::LineFrameCornerRadius,
        UiIds::Control::PhoneOutsideLinePrefix,
        UiIds::Control::PhoneLongDistancePrefix,
        UiIds::Control::PhoneLocalAreaCode,
    };
    return numericControls.contains(controlId) ||
        (dialogName == QStringLiteral("TPLDATAFRAME") && controlId == 4210) ||
        (dialogName == QStringLiteral("IMPEDIT") && controlId == 706);
}

void applyInterestingDialogState(QDialog* dialog)
{
    if (dialog == nullptr) {
        return;
    }

    for (QComboBox* comboBox : dialog->findChildren<QComboBox*>()) {
        const int widestIndex = widestComboItemIndex(comboBox);
        if (widestIndex >= 0) {
            comboBox->setCurrentIndex(widestIndex);
        }
    }

    for (QListWidget* listWidget : dialog->findChildren<QListWidget*>()) {
        if (listWidget->count() > 0) {
            listWidget->setCurrentRow(listWidget->count() - 1);
        }
    }

    for (QLineEdit* lineEdit : dialog->findChildren<QLineEdit*>()) {
        if (lineEdit->isReadOnly()) {
            continue;
        }
        if (qobject_cast<QComboBox*>(lineEdit->parentWidget()) != nullptr) {
            continue;
        }
        if (isNumericStressEdit(lineEdit)) {
            lineEdit->setText(QStringLiteral("999"));
            continue;
        }

        const int usefulLength = lineEdit->maxLength() > 0
            ? std::min(lineEdit->maxLength(), 28)
            : 28;
        const QString sample = QStringLiteral("Wide sample value 1234567890").left(usefulLength);
        lineEdit->setText(sample);
    }

    for (QSpinBox* spinBox : dialog->findChildren<QSpinBox*>()) {
        spinBox->setValue(spinBox->maximum());
    }
}

bool saveDialogImage(QDialog* dialog, const QDir& outputDirectory, const QString& fileStem)
{
    if (dialog == nullptr) {
        return false;
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();

    const QPixmap pixmap = dialog->grab();
    if (pixmap.isNull()) {
        return false;
    }

    return pixmap.save(outputDirectory.filePath(fileStem + QStringLiteral(".png")));
}

QString clickHelpButtonAndCaptureText(QAbstractButton* helpButton)
{
    QString helpText;
    bool closed = false;
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();

    *retry = [&helpText, &closed, attemptsRemaining, retry]() {
        if (closeVisibleHelpDialog(&helpText)) {
            closed = true;
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, helpButton, [helpButton]() {
        helpButton->click();
    });
    QTimer::singleShot(10, qApp, *retry);
    for (int attempt = 0; attempt < 100 && !closed; ++attempt) {
        QTest::qWait(10);
    }
    return helpText;
}

} // namespace

class UiBuilderTests : public QObject {
    Q_OBJECT

private slots:
    void populatesMainDeckMenuCommands()
    {
        QMenuBar startupMenuBar;
        QVERIFY(UiBuilder::populateMenuBar(&startupMenuBar, UiIds::Menu::Startup, &startupMenuBar, [](QAction*) {}));
        const QSet<int> startupCommandIds = commandIdsFromMenuBar(&startupMenuBar);
        QVERIFY(startupCommandIds.contains(UiIds::Command::FileNew));
        QVERIFY(startupCommandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(startupCommandIds.contains(UiIds::Command::FilePrinterSetup));
        QVERIFY(startupCommandIds.contains(UiIds::Command::HelpContents));
        QVERIFY(!startupCommandIds.contains(UiIds::Command::FileSave));

        QMenuBar menuBar;
        QVERIFY(UiBuilder::populateMenuBar(&menuBar, UiIds::Menu::MainDeck, &menuBar, [](QAction*) {}));

        const QSet<int> commandIds = commandIdsFromMenuBar(&menuBar);
        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(commandIds.contains(UiIds::Command::FilePrintReport));
        QVERIFY(commandIds.contains(UiIds::Command::SearchFind));
        QVERIFY(commandIds.contains(UiIds::Command::SearchReplace));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureDeckDescription));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureAddSecurity));
        QVERIFY(commandIds.contains(UiIds::Command::PhoneDial));
        QVERIFY(commandIds.contains(UiIds::Command::HelpAbout));
    }

    void populatesTemplateDesignerMenuCommands()
    {
        QMenuBar menuBar;
        QVERIFY(UiBuilder::populateMenuBar(&menuBar, UiIds::Menu::TemplateDesigner, &menuBar, [](QAction*) {}));

        const QSet<int> commandIds = commandIdsFromMenuBar(&menuBar);
        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddDataBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddNotesBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolFrameAttributes));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureDeckDescription));
        QVERIFY(commandIds.contains(UiIds::Command::HelpContents));
    }

    void populatesReportDesignerMenuCommands()
    {
        QMenuBar menuBar;
        QVERIFY(UiBuilder::populateMenuBar(&menuBar, UiIds::Menu::ReportDesigner, &menuBar, [](QAction*) {}));

        const QSet<int> commandIds = commandIdsFromMenuBar(&menuBar);
        QVERIFY(commandIds.contains(UiIds::Command::FileSaveReport));
        QVERIFY(commandIds.contains(UiIds::Command::FileSaveReportAs));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddDataBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddSystemData));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddLineOrBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolChangeForm));
    }

    void createsEveryDecodedDialog()
    {
        const UiBuilder::DialogContext context = populatedContext();
        const QStringList dialogNames = UiBuilder::dialogNames();
        QVERIFY(dialogNames.size() > 20);

        for (const QString& dialogName : dialogNames) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Dialog did not instantiate: %1").arg(dialogName)));
            QVERIFY2(
                !dialog->findChildren<QWidget*>().isEmpty(),
                qPrintable(QStringLiteral("Dialog has no child widgets: %1").arg(dialogName)));
        }
    }


    void everyCompiledMenuResourcePopulates()
    {
        QVERIFY(UiResourceData::menuCount() >= 4);
        for (std::size_t index = 0; index < UiResourceData::menuCount(); ++index) {
            const UiResourceData::UiMenu& menu = UiResourceData::menus()[index];
            QMenuBar menuBar;
            QVERIFY2(
                UiBuilder::populateMenuBar(&menuBar, menu.name, &menuBar, [](QAction*) {}),
                qPrintable(QStringLiteral("Menu did not populate: %1").arg(menu.name)));

            const QSet<int> expectedCommands = commandIdsFromMenuResource(menu);
            const QSet<int> actualCommands = commandIdsFromMenuBar(&menuBar);
            for (int commandId : expectedCommands) {
                QVERIFY2(
                    actualCommands.contains(commandId),
                    qPrintable(QStringLiteral("Menu %1 did not materialize command %2").arg(menu.name).arg(commandId)));
            }
        }
    }

    void everyCompiledDialogControlMaterializes()
    {
        const UiBuilder::DialogContext context = populatedContext();
        for (std::size_t index = 0; index < UiResourceData::dialogCount(); ++index) {
            const UiResourceData::UiDialog& dialogResource = UiResourceData::dialogs()[index];
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QString::fromUtf8(dialogResource.name), nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Dialog did not instantiate: %1").arg(dialogResource.name)));

            const QList<QWidget*> directControls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            QVERIFY2(
                directControls.size() >= static_cast<int>(dialogResource.controlCount),
                qPrintable(QStringLiteral("Dialog %1 materialized %2 controls from %3 resource controls")
                    .arg(dialogResource.name)
                    .arg(directControls.size())
                    .arg(dialogResource.controlCount)));

            for (std::size_t controlIndex = 0; controlIndex < dialogResource.controlCount; ++controlIndex) {
                const UiResourceData::UiControl& control = dialogResource.controls[controlIndex];
                if (control.id == 65535) {
                    continue;
                }
                QVERIFY2(
                    UiBuilder::controlById(dialog.get(), control.id) != nullptr,
                    qPrintable(QStringLiteral("Dialog %1 did not expose control id %2")
                        .arg(dialogResource.name)
                        .arg(control.id)));
            }
        }
    }

    void everyNamedStringResourceResolves()
    {
        QSet<int> requiredStringIds = {
            UiIds::StringId::AllDataBoxes,
            UiIds::StringId::NewFromTemplate,
            UiIds::StringId::NewFromScratch,
            UiIds::StringId::NewPatternedAfterTemplate,
            UiIds::StringId::NewPatternedAfterDeck,
        };
        for (int id = UiIds::StringId::SearchTypeFirst; id <= UiIds::StringId::SearchTypeLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::ColorRoleFirst; id <= UiIds::StringId::ColorRoleLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxDateFormatFirst; id <= UiIds::StringId::SystemBoxDateFormatLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxNumberFormatFirst; id <= UiIds::StringId::SystemBoxNumberFormatLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxFieldFirst; id <= UiIds::StringId::SystemBoxFieldLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxDateTokenFirst; id <= UiIds::StringId::SystemBoxDateTokenLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxNumberTokenFirst; id <= UiIds::StringId::SystemBoxNumberTokenLast; ++id) {
            requiredStringIds.insert(id);
        }
        for (int id = UiIds::StringId::SystemBoxFieldTokenFirst; id <= UiIds::StringId::SystemBoxFieldTokenLast; ++id) {
            requiredStringIds.insert(id);
        }

        for (int stringId : requiredStringIds) {
            const UiResourceData::UiString* stringValue = UiResourceData::findString(stringId);
            QVERIFY2(
                stringValue != nullptr && stringValue->text != nullptr && QString::fromUtf8(stringValue->text).trimmed().size() > 0,
                qPrintable(QStringLiteral("Required UI resource string is missing or blank: %1").arg(stringId)));
        }
    }

    void initializesNewFileDialogFromContext()
    {
        const UiBuilder::DialogContext context = populatedContext();
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("NEWFILE"), nullptr, context);
        QVERIFY(dialog != nullptr);

        auto* sourceCombo = qobject_cast<QComboBox*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::NewFileSourceCombo));
        QVERIFY(sourceCombo != nullptr);
        QVERIFY(sourceCombo->count() >= 4);

        auto* templateList = qobject_cast<QListWidget*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::NewFileTemplateList));
        QVERIFY(templateList != nullptr);
        QCOMPARE(templateList->count(), context.templateNames.size());

        QVERIFY(sourceCombo->width() >= 220);
        QVERIFY(sourceCombo->view() != nullptr);
        QVERIFY(sourceCombo->view()->minimumWidth() >= 280);
    }

    void initializesSecurityDialogForPasswordEntry()
    {
        const UiBuilder::DialogContext context = populatedContext();
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("ADDSECURITY"), nullptr, context);
        QVERIFY(dialog != nullptr);

        auto* password = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::SecurityPassword));
        QVERIFY(password != nullptr);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
    }

    void recoveredListActivationNotificationsUseTheirLegacyCommandPaths()
    {
        const UiBuilder::DialogContext context = populatedContext();

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("REPORTFORM"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* formList = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::ReportFormList));
            QVERIFY(formList != nullptr);
            formList->addItem(QStringLiteral("Letter"));
            formList->setCurrentRow(0);
            QVERIFY(QMetaObject::invokeMethod(
                formList,
                "itemDoubleClicked",
                Qt::DirectConnection,
                Q_ARG(QListWidgetItem*, formList->currentItem())));
            QCOMPARE(dialog->result(), static_cast<int>(QDialog::Accepted));
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SAVEDESIGN"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* reportList = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SaveDesignList));
            auto* nameEdit = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SaveDesignName));
            QVERIFY(reportList != nullptr);
            QVERIFY(nameEdit != nullptr);
            QVERIFY(reportList->count() > 1);

            reportList->setCurrentRow(1);
            QCOMPARE(nameEdit->text(), reportList->currentItem()->text());
        }
    }

    void recoveredControlChangeNotificationsUseTheirLegacyCommandPaths()
    {
        const UiBuilder::DialogContext context = populatedContext();

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("PRINT"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* copies = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintCopyCount));
            auto* copySpin = qobject_cast<QSpinBox*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintCopyCountSpin));
            auto* selected = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintSelectedCards));
            auto* all = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintAllCards));
            auto* defineSearch = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintDefineSearch));
            auto* summary = qobject_cast<QLabel*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PrintSummary1));
            QVERIFY(copies != nullptr);
            QVERIFY(copySpin != nullptr);
            QVERIFY(selected != nullptr);
            QVERIFY(all != nullptr);
            QVERIFY(defineSearch != nullptr);
            QVERIFY(summary != nullptr);

            copies->setText(QStringLiteral("0"));
            QCOMPARE(copies->text(), QStringLiteral("1"));
            copies->setText(QStringLiteral("10001"));
            QCOMPARE(copies->text(), QStringLiteral("10000"));
            copies->setText(QStringLiteral("9"));
            copySpin->setValue(1);
            QCOMPARE(copies->text(), QStringLiteral("10"));

            QVERIFY(!defineSearch->isEnabled());
            selected->setChecked(true);
            QVERIFY(defineSearch->isEnabled());
            QVERIFY(summary->text().contains(QStringLiteral("selection"), Qt::CaseInsensitive));
            all->setChecked(true);
            QVERIFY(!defineSearch->isEnabled());
            QVERIFY(summary->text().contains(QStringLiteral("all cards"), Qt::CaseInsensitive));
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("NEWFILE"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* source = qobject_cast<QComboBox*>(UiBuilder::controlById(dialog.get(), UiIds::Control::NewFileSourceCombo));
            auto* templates = qobject_cast<QListWidget*>(UiBuilder::controlById(dialog.get(), UiIds::Control::NewFileTemplateList));
            QVERIFY(source != nullptr);
            QVERIFY(templates != nullptr);
            QVERIFY(templates->count() > 1);
            templates->setCurrentRow(1);
            source->setCurrentIndex(1);
            QVERIFY(!templates->isEnabled());
            templates->setCurrentRow(-1);
            source->setCurrentIndex(2);
            QVERIFY(templates->isEnabled());
            QCOMPARE(templates->currentRow(), 1);
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SORT"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* secondLevel = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SortFieldLevel2));
            auto* thirdLevel = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SortFieldLevel3));
            QVERIFY(secondLevel != nullptr);
            QVERIFY(thirdLevel != nullptr);
            dialog->show();
            secondLevel->setCurrentIndex(1);
            secondLevel->setFocus();
            secondLevel->setCurrentIndex(0);
            QMetaObject::invokeMethod(secondLevel, "activated", Qt::DirectConnection, Q_ARG(int, 0));
            QCOMPARE(dialog->focusWidget(), thirdLevel);
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SEARCH"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* none = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::SearchCompareNone));
            auto* andButton = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::SearchCompareAnd));
            QWidget* secondText = UiBuilder::controlById(dialog.get(), UiIds::Control::SearchSecondText);
            QVERIFY(none != nullptr);
            QVERIFY(andButton != nullptr);
            QVERIFY(secondText != nullptr);
            andButton->setChecked(true);
            QVERIFY(!secondText->isHidden());
            QVERIFY(secondText->isEnabled());
            none->setChecked(true);
            QVERIFY(secondText->isHidden());
            QVERIFY(!secondText->isEnabled());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("REPLACE"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* currentData = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog.get(), UiIds::Control::ReplaceCurrentData));
            QWidget* replaceButton = UiBuilder::controlById(dialog.get(), UiIds::Control::Ok);
            QVERIFY(currentData != nullptr);
            QVERIFY(replaceButton != nullptr);
            QVERIFY(currentData->isReadOnly());
            QCOMPARE(currentData->focusProxy(), replaceButton);
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("IMPEDIT"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* notes = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::ImportEditNotes));
            QWidget* length = UiBuilder::controlById(dialog.get(), UiIds::Control::ImportEditLength);
            QVERIFY(notes != nullptr);
            QVERIFY(length != nullptr);
            notes->setChecked(true);
            QVERIFY(!length->isEnabled());
            notes->setChecked(false);
            QVERIFY(length->isEnabled());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("LINEFRAME"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* horizontal = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::LineFrameHorizontal));
            auto* lineStyle = qobject_cast<QComboBox*>(UiBuilder::controlById(dialog.get(), UiIds::Control::LineFrameLineStyle));
            auto* fillPattern = qobject_cast<QComboBox*>(UiBuilder::controlById(dialog.get(), UiIds::Control::LineFrameFillPattern));
            auto* radius = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog.get(), UiIds::Control::LineFrameCornerRadius));
            QWidget* preview = UiBuilder::controlById(dialog.get(), UiIds::Control::LineFramePreview);
            QVERIFY(horizontal != nullptr);
            QVERIFY(lineStyle != nullptr);
            QVERIFY(fillPattern != nullptr);
            QVERIFY(radius != nullptr);
            QVERIFY(preview != nullptr);
            lineStyle->addItems({QStringLiteral("Solid"), QStringLiteral("Dash")});
            fillPattern->addItems({QStringLiteral("Clear"), QStringLiteral("10%")});
            horizontal->setChecked(true);
            lineStyle->setCurrentIndex(1);
            fillPattern->setCurrentIndex(1);
            radius->setText(QStringLiteral("250"));
            QCOMPARE(radius->text(), QStringLiteral("200"));
            QCOMPARE(preview->property("lineFrameShape").toInt(), 1);
            QCOMPARE(preview->property("lineFrameLineStyle").toInt(), 1);
            QCOMPARE(preview->property("lineFrameFillPattern").toInt(), 1);
            QCOMPARE(preview->property("lineFrameCornerRadius").toInt(), 200);
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("ADDSECURITY"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* password = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog.get(), UiIds::Control::SecurityPassword));
            QVERIFY(password != nullptr);
            password->setText(QStringLiteral("mixedCase"));
            QCOMPARE(password->text(), QStringLiteral("MIXEDCAS"));
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("ADDSYSTEMBOX"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* numberCategory = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxNumberCategory));
            auto* systemCategory = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxSystemCategory));
            auto* numberFormats = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxNumberFormats));
            auto* systemFields = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxSystemFields));
            QVERIFY(numberCategory != nullptr);
            QVERIFY(systemCategory != nullptr);
            QVERIFY(numberFormats != nullptr);
            QVERIFY(systemFields != nullptr);
            numberFormats->setCurrentIndex(1);
            QVERIFY(numberCategory->isChecked());
            systemFields->setCurrentIndex(1);
            QVERIFY(systemCategory->isChecked());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("QUICKDIAL"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* number = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::QuickDialNumber));
            auto* ok = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::Ok));
            QVERIFY(number != nullptr);
            QVERIFY(ok != nullptr);
            QVERIFY(!ok->isEnabled());
            number->setText(QStringLiteral("555-1212"));
            QVERIFY(ok->isEnabled());
            number->clear();
            QVERIFY(!ok->isEnabled());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("PHNDEF"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* quickDials = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneQuickDials));
            auto* add = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneQuickDialAdd));
            auto* modify = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneQuickDialModify));
            auto* remove = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneQuickDialDelete));
            QVERIFY(quickDials != nullptr);
            QVERIFY(add != nullptr);
            QVERIFY(modify != nullptr);
            QVERIFY(remove != nullptr);
            QVERIFY(add->isEnabled());
            QVERIFY(!modify->isEnabled());
            QVERIFY(!remove->isEnabled());
            quickDials->addItem(QStringLiteral("First\t555-1212"));
            QVERIFY(modify->isEnabled());
            QVERIFY(remove->isEnabled());
            while (quickDials->count() < 100) {
                quickDials->addItem(QString::number(quickDials->count()));
            }
            QVERIFY(!add->isEnabled());
            quickDials->clear();
            QVERIFY(add->isEnabled());
            QVERIFY(!modify->isEnabled());
            QVERIFY(!remove->isEnabled());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("DESIGNREPORTS"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* reports = qobject_cast<QListWidget*>(UiBuilder::controlById(dialog.get(), UiIds::Control::ReportsList));
            auto* modify = qobject_cast<QAbstractButton*>(UiBuilder::controlById(dialog.get(), UiIds::Control::ReportsModify));
            QVERIFY(reports != nullptr);
            QVERIFY(modify != nullptr);
            reports->setCurrentRow(-1);
            QVERIFY(!modify->isEnabled());
            reports->setCurrentRow(0);
            QVERIFY(modify->isEnabled());
        }
    }

    void initializesExpectedRadioButtonDefaults()
    {
        const UiBuilder::DialogContext context = populatedContext();

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("REPORTFORM"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* report = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::ReportFormReport));
            QVERIFY(report != nullptr);
            QVERIFY(report->isChecked());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* report = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::DefineFormReport));
            auto* portrait = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::DefineFormPortrait));
            QVERIFY(report != nullptr);
            QVERIFY(portrait != nullptr);
            QVERIFY(report->isChecked());
            QVERIFY(portrait->isChecked());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("SEARCH"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* noCompare = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchCompareNone));
            auto* andCompare = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchCompareAnd));
            auto* orCompare = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchCompareOr));
            auto* beginning = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchDirectionBeginning));
            auto* dataCombo = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchAllDataBoxes));
            auto* typeCombo = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchType));
            QWidget* secondText = UiBuilder::controlById(dialog.get(), UiIds::Control::SearchSecondText);
            QVERIFY(noCompare != nullptr);
            QVERIFY(andCompare != nullptr);
            QVERIFY(orCompare != nullptr);
            QVERIFY(beginning != nullptr);
            QVERIFY(dataCombo != nullptr);
            QVERIFY(typeCombo != nullptr);
            QVERIFY(secondText != nullptr);
            QVERIFY(noCompare->isChecked());
            QVERIFY(beginning->isChecked());
            QVERIFY(typeCombo->width() >= 180);
            QVERIFY(typeCombo->width() <= 190);
            QVERIFY(secondText->isHidden());
            const int compactHeight = dialog->height();

            andCompare->click();
            QCoreApplication::processEvents();
            QVERIFY(andCompare->isChecked());
            QVERIFY(!secondText->isHidden());
            const int expandedHeight = dialog->height();
            QVERIFY(expandedHeight > compactHeight);

            orCompare->click();
            QCoreApplication::processEvents();
            QVERIFY(orCompare->isChecked());
            QVERIFY(!secondText->isHidden());

            noCompare->click();
            QCoreApplication::processEvents();
            QVERIFY(noCompare->isChecked());
            QVERIFY(secondText->isHidden());
            QCOMPARE(dialog->height(), compactHeight);
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CALL"), nullptr, context);
            QVERIFY(dialog != nullptr);
            QLabel* phoneIcon = nullptr;
            for (QLabel* label : dialog->findChildren<QLabel*>()) {
                if (label->property("legacyIconResource").toString() == QStringLiteral("IDPHONE")) {
                    phoneIcon = label;
                    break;
                }
            }
            QVERIFY(phoneIcon != nullptr);
            QVERIFY(phoneIcon->text().isEmpty());
            QVERIFY(!phoneIcon->pixmap().isNull());
            QCOMPARE(phoneIcon->accessibleName(), QStringLiteral("Phone"));
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("ADDSYSTEMBOX"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* date = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxDateCategory));
            auto* left = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SystemBoxLeft));
            QVERIFY(date != nullptr);
            QVERIFY(left != nullptr);
            QVERIFY(date->isChecked());
            QVERIFY(left->isChecked());
        }

        {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("TEXTFRAME"), nullptr, context);
            QVERIFY(dialog != nullptr);
            auto* left = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::FrameAlignmentLeft));
            QVERIFY(left != nullptr);
            QVERIFY(left->isChecked());
        }
    }

    void initializesColorDialogRolesAndSystemPaletteMode()
    {
        const UiBuilder::DialogContext context = populatedContext();
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"), nullptr, context);
        QVERIFY(dialog != nullptr);

        auto* roleCombo = qobject_cast<QComboBox*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::ColorRoleCombo));
        auto* swatchGrid = UiBuilder::controlById(dialog.get(), UiIds::Control::ColorCustomGrid);
        auto* deckPreview = dialog->findChild<QWidget*>(QStringLiteral("deckColorPreview"));
        auto* useSystem = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::ColorUseSystem));

        QVERIFY(roleCombo != nullptr);
        QVERIFY(swatchGrid != nullptr);
        QVERIFY(deckPreview != nullptr);
        QVERIFY(useSystem != nullptr);
        QCOMPARE(roleCombo->count(), UiIds::StringId::ColorRoleLast - UiIds::StringId::ColorRoleFirst + 1);
        QCOMPARE(roleCombo->currentIndex(), 5);
        QVERIFY(useSystem->isCheckable());
        QVERIFY(!useSystem->isHidden());
        QCOMPARE(swatchGrid->property("paletteColorCount").toInt(), 48);
        QVERIFY(!swatchGrid->property("hasDeckPreview").toBool());
        QVERIFY(deckPreview->property("hasDeckPreview").toBool());
        QCOMPARE(deckPreview->geometry().left(), roleCombo->geometry().left());
        QVERIFY(deckPreview->geometry().right() < swatchGrid->geometry().left());
        roleCombo->setCurrentIndex(1);
        useSystem->setChecked(true);
        QCoreApplication::processEvents();
        QCOMPARE(roleCombo->currentIndex(), 1);
        QVERIFY(useSystem->isChecked());

        const QStringList customColors = {
            QStringLiteral("#010203"), QStringLiteral("#111213"), QStringLiteral("#212223"),
            QStringLiteral("#313233"), QStringLiteral("#414243"), QStringLiteral("#515253"),
            QStringLiteral("#616263")};
        UiBuilder::setColorDialogState(dialog.get(), customColors, false);
        QCOMPARE(UiBuilder::colorDialogColors(dialog.get()), customColors);
        QVERIFY(!UiBuilder::colorDialogUsesSystemColors(dialog.get()));
    }

    void everyVisibleDialogControlHasSafeGeometry()
    {
        const UiBuilder::DialogContext context = populatedContext();
        for (const QString& dialogName : UiBuilder::dialogNames()) {
            if (isToolbarResourceName(dialogName)) {
                continue;
            }
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Dialog did not instantiate: %1").arg(dialogName)));

            const QRect safeDialogRect = dialog->rect().adjusted(-1, -1, 1, 1);
            const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget* widget : controls) {
                if (widget->isHidden()) {
                    continue;
                }

                QVERIFY2(
                    widget->geometry().isValid(),
                    qPrintable(QStringLiteral("%1 has invalid geometry: %2").arg(dialogName, widgetDebugName(widget))));
                QVERIFY2(
                    safeDialogRect.contains(widget->geometry()),
                    qPrintable(QStringLiteral("%1 has out-of-bounds control: %2 dialog=%3x%4")
                        .arg(dialogName, widgetDebugName(widget))
                        .arg(dialog->width())
                        .arg(dialog->height())));
            }

            for (int firstIndex = 0; firstIndex < controls.size(); ++firstIndex) {
                for (int secondIndex = firstIndex + 1; secondIndex < controls.size(); ++secondIndex) {
                    QWidget* first = controls.at(firstIndex);
                    QWidget* second = controls.at(secondIndex);
                    QVERIFY2(
                        !hasMaterialOverlap(first, second),
                        qPrintable(QStringLiteral("%1 has overlapping controls:\n  %2\n  %3")
                            .arg(dialogName, widgetDebugName(first), widgetDebugName(second))));
                }
            }

            for (QWidget* widget : controls) {
                QVERIFY2(
                    textFitsWidget(widget),
                    qPrintable(QStringLiteral("%1 has elided text: %2").arg(dialogName, widgetDebugName(widget))));
            }

            const QList<QGroupBox*> groupBoxes = dialog->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
            for (int firstIndex = 0; firstIndex < groupBoxes.size(); ++firstIndex) {
                if (groupBoxes.at(firstIndex)->isHidden()) {
                    continue;
                }
                const QRect firstTitle = groupTitleCollisionRect(groupBoxes.at(firstIndex));
                if (!firstTitle.isValid()) {
                    continue;
                }
                for (int secondIndex = 0; secondIndex < controls.size(); ++secondIndex) {
                    QWidget* second = controls.at(secondIndex);
                    if (second == groupBoxes.at(firstIndex) || second->isHidden()) {
                        continue;
                    }
                    const QRect titleOverlap = firstTitle.intersected(second->geometry());
                    QVERIFY2(
                        !titleOverlap.isValid() || titleOverlap.height() <= 2,
                        qPrintable(QStringLiteral("%1 has group title colliding with control:\n  title=%2,%3 %4x%5\n  %6")
                            .arg(dialogName)
                            .arg(firstTitle.x())
                            .arg(firstTitle.y())
                            .arg(firstTitle.width())
                            .arg(firstTitle.height())
                            .arg(widgetDebugName(second))));
                }
            }
        }
    }

    void everyPopulatedComboBoxHasUsablePopupGeometry()
    {
        const UiBuilder::DialogContext context = populatedContext();
        for (const QString& dialogName : UiBuilder::dialogNames()) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Dialog did not instantiate: %1").arg(dialogName)));

            const QList<QComboBox*> comboBoxes = dialog->findChildren<QComboBox*>();
            for (QComboBox* comboBox : comboBoxes) {
                if (comboBox->isHidden() || comboBox->count() == 0) {
                    continue;
                }

                QVERIFY2(
                    comboBox->view() != nullptr,
                    qPrintable(QStringLiteral("%1 has combobox without popup view: %2")
                        .arg(dialogName, widgetDebugName(comboBox))));
                QVERIFY2(
                    effectiveComboPopupWidth(comboBox) >= requiredComboPopupWidth(comboBox),
                    qPrintable(QStringLiteral("%1 has too-narrow combobox popup: %2 required=%3 effective=%4")
                        .arg(dialogName, widgetDebugName(comboBox))
                        .arg(requiredComboPopupWidth(comboBox))
                        .arg(effectiveComboPopupWidth(comboBox))));
                QVERIFY2(
                    comboBox->maxVisibleItems() >= std::min(comboBox->count(), 4),
                    qPrintable(QStringLiteral("%1 has too-few combobox popup rows: %2 count=%3 maxVisible=%4")
                        .arg(dialogName, widgetDebugName(comboBox))
                        .arg(comboBox->count())
                        .arg(comboBox->maxVisibleItems())));
                if (!comboBox->isEditable()) {
                    int widestIndex = 0;
                    for (int index = 1; index < comboBox->count(); ++index) {
                        if (comboBox->fontMetrics().horizontalAdvance(comboBox->itemText(index)) >
                            comboBox->fontMetrics().horizontalAdvance(comboBox->itemText(widestIndex))) {
                            widestIndex = index;
                        }
                    }
                    comboBox->setCurrentIndex(widestIndex);
                    const int requiredTextWidth = comboBox->fontMetrics().horizontalAdvance(comboBox->currentText());
                    QVERIFY2(
                        closedComboTextAreaWidth(comboBox) >= requiredTextWidth,
                        qPrintable(QStringLiteral("%1 clips its widest closed combobox selection after reserving the arrow: %2 required=%3 available=%4")
                            .arg(dialogName, widgetDebugName(comboBox))
                            .arg(requiredTextWidth)
                            .arg(closedComboTextAreaWidth(comboBox))));
                }
            }
        }
    }

    void findAndReplaceSearchLabelsHaveComfortableClearance()
    {
        const UiBuilder::DialogContext context = populatedContext();
        for (const QString& dialogName : {QStringLiteral("SEARCH"), QStringLiteral("REPLACE")}) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(dialogName));
            for (QLabel* label : dialog->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (plainVisibleText(label->text()) != QStringLiteral("Search in data box...")) {
                    continue;
                }
                QComboBox* nearestAbove = nullptr;
                int nearestBottom = -1;
                for (QComboBox* comboBox : dialog->findChildren<QComboBox*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (comboBox->geometry().bottom() >= label->geometry().top() ||
                        comboBox->geometry().right() < label->geometry().left() ||
                        comboBox->geometry().left() > label->geometry().right()) {
                        continue;
                    }
                    if (comboBox->geometry().bottom() > nearestBottom) {
                        nearestAbove = comboBox;
                        nearestBottom = comboBox->geometry().bottom();
                    }
                }
                QVERIFY2(nearestAbove != nullptr, qPrintable(QStringLiteral("%1 search label has no preceding combo").arg(dialogName)));
                const int clearance = label->geometry().top() - nearestAbove->geometry().bottom() - 1;
                QVERIFY2(
                    clearance >= 5,
                    qPrintable(QStringLiteral("%1 search label clearance is only %2 px; label=%3,%4 %5x%6; combo=%7,%8 %9x%10")
                            .arg(dialogName)
                            .arg(clearance)
                            .arg(label->x())
                            .arg(label->y())
                            .arg(label->width())
                            .arg(label->height())
                            .arg(nearestAbove->x())
                            .arg(nearestAbove->y())
                            .arg(nearestAbove->width())
                            .arg(nearestAbove->height())));
            }
        }
    }

    void modernizesPhoneConfigurationDialogLayout()
    {
        const UiBuilder::DialogContext context = populatedContext();
        std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("PHNDEF"), nullptr, context);
        QVERIFY(dialog != nullptr);

        const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* widget : controls) {
            auto* button = qobject_cast<QAbstractButton*>(widget);
            if (button == nullptr) {
                continue;
            }

            const QString text = button->text();
            if (text.contains(QStringLiteral("COM")) ||
                text == QStringLiteral("Port") ||
                text == QStringLiteral("Dial Method") ||
                text == QStringLiteral("Initialization") ||
                text.contains(QStringLiteral("Tone")) ||
                text.contains(QStringLiteral("Pulse")) ||
                text.contains(QStringLiteral("default")) ||
                text.contains(QStringLiteral("custom"))) {
                QVERIFY2(widget->isHidden(), qPrintable(QStringLiteral("Obsolete phone control should be hidden: %1").arg(text)));
            }
        }

        auto* longDistance = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneLongDistance));
        auto* outsideLine = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneOutsideLine));
        auto* quickDials = qobject_cast<QListWidget*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneQuickDials));
        auto* longDistancePrefix = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneLongDistancePrefix));
        auto* outsideLinePrefix = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneOutsideLinePrefix));
        auto* localAreaCode = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PhoneLocalAreaCode));

        QVERIFY(longDistance != nullptr);
        QVERIFY(outsideLine != nullptr);
        QVERIFY(quickDials != nullptr);
        QVERIFY(longDistancePrefix != nullptr);
        QVERIFY(outsideLinePrefix != nullptr);
        QVERIFY(localAreaCode != nullptr);
        QVERIFY(!longDistance->isHidden());
        QVERIFY(!outsideLine->isHidden());
        QVERIFY(!quickDials->isHidden());
        QVERIFY(longDistance->geometry().top() < 70);
        QVERIFY(quickDials->geometry().top() > 100);
        QVERIFY(longDistancePrefix->width() <= 54);
        QVERIFY(outsideLinePrefix->width() <= 54);
        QVERIFY(localAreaCode->width() <= 54);
    }

    void keepsShortNumericAndMicroScrollControlsCompact()
    {
        const UiBuilder::DialogContext context = populatedContext();

        std::unique_ptr<QDialog> printDialog = UiBuilder::createDialog(QStringLiteral("PRINT"), nullptr, context);
        QVERIFY(printDialog != nullptr);
        auto* copies = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(printDialog.get(), UiIds::Control::PrintCopyCount));
        QVERIFY(copies != nullptr);
        QVERIFY(copies->width() <= 54);

        bool foundPrintMicroScroll = false;
        for (QWidget* widget : printDialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (widget->property("uiControlClass").toString() == QStringLiteral("MicroScroll")) {
                foundPrintMicroScroll = true;
                QVERIFY(widget->width() <= 12);
                QVERIFY(widget->height() >= 12);
            }
        }
        QVERIFY(foundPrintMicroScroll);

        std::unique_ptr<QDialog> defineFormDialog = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"), nullptr, context);
        QVERIFY(defineFormDialog != nullptr);
        bool foundDefineFormMicroScroll = false;
        for (QWidget* widget : defineFormDialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (widget->property("uiControlClass").toString() == QStringLiteral("MicroScroll")) {
                foundDefineFormMicroScroll = true;
                QVERIFY(widget->width() <= 12);
                QVERIFY(widget->height() >= 12);
            }
        }
        QVERIFY(foundDefineFormMicroScroll);
    }

    void keepsMicroScrollControlsAttachedToLineEdits()
    {
        const UiBuilder::DialogContext context = populatedContext();
        int checkedMicroScrolls = 0;

        for (const QString& dialogName : UiBuilder::dialogNames()) {
            if (dialogName == QStringLiteral("PRINT") ||
                dialogName == QStringLiteral("LINEFRAME")) {
                continue;
            }

            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(dialogName));

            const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget* widget : controls) {
                if (widget->isHidden() ||
                    widget->property("uiControlClass").toString() != QStringLiteral("MicroScroll")) {
                    continue;
                }

                const QRect microRect = widget->geometry();
                if (microRect.width() > 12) {
                    ++checkedMicroScrolls;
                    continue;
                }

                QWidget* attachedEdit = nullptr;
                bool hasPeerEdit = false;
                for (QWidget* candidate : controls) {
                    auto* edit = qobject_cast<QLineEdit*>(candidate);
                    if (edit == nullptr || edit->isHidden()) {
                        continue;
                    }

                    const QRect editRect = edit->geometry();
                    if (editRect.left() >= microRect.left()) {
                        continue;
                    }

                    const bool verticallyAligned =
                        std::abs(editRect.center().y() - microRect.center().y()) <= 4;
                    if (!verticallyAligned) {
                        continue;
                    }

                    hasPeerEdit = true;
                    const bool horizontallyAttached =
                        std::abs((editRect.right() + 1) - microRect.left()) <= 4;
                    const bool sameHeight =
                        std::abs(editRect.height() - microRect.height()) <= 4;
                    if (horizontallyAttached && sameHeight) {
                        attachedEdit = edit;
                        break;
                    }
                }

                if (!hasPeerEdit) {
                    ++checkedMicroScrolls;
                    continue;
                }

                QVERIFY2(
                    attachedEdit != nullptr,
                    qPrintable(QStringLiteral("Detached micro-scroll in %1 at %2,%3 %4x%5")
                        .arg(dialogName)
                        .arg(microRect.x())
                        .arg(microRect.y())
                        .arg(microRect.width())
                        .arg(microRect.height())));
                ++checkedMicroScrolls;
            }
        }

        QVERIFY(checkedMicroScrolls > 0);
    }

    void everyHelpButtonOpensContextHelp()
    {
        const UiBuilder::DialogContext context = populatedContext();
        const QVector<std::pair<QString, QString>> helpExpectations = {
            {QStringLiteral("SORT"), QStringLiteral("up to three data boxes")},
            {QStringLiteral("SEARCH"), QStringLiteral("Find searches")},
            {QStringLiteral("MERGEDLG"), QStringLiteral("Merge copies cards")},
            {QStringLiteral("EXPORT"), QStringLiteral("Export writes cards")},
            {QStringLiteral("NEWFILE"), QStringLiteral("Design deck from scratch")},
            {QStringLiteral("PRINT"), QStringLiteral("Print Preview renders")},
            {QStringLiteral("CALL"), QStringLiteral("opens calls through")},
            {QStringLiteral("PHNDEF"), QStringLiteral("opens calls through")},
            {QStringLiteral("IMPEXAMINE"), QStringLiteral("review field names")},
            {QStringLiteral("CHOOSECOLOR"), QStringLiteral("System palette mode")},
            {QStringLiteral("DESIGNREPORTS"), QStringLiteral("manages report designs")},
            {QStringLiteral("SAVEDESIGN"), QStringLiteral("save it into the active deck")},
            {QStringLiteral("REPORTFORM"), QStringLiteral("printable page or label grid")},
            {QStringLiteral("ADDSYSTEMBOX"), QStringLiteral("generated values")},
            {QStringLiteral("REPLACE"), QStringLiteral("Replace finds matching data")},
            {QStringLiteral("GETUSERNAME"), QStringLiteral("display name")},
        };

        for (const auto& expectation : helpExpectations) {
            const QString& dialogName = expectation.first;
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(dialogName));

            auto* helpButton = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::Help));
            QVERIFY2(helpButton != nullptr, qPrintable(QStringLiteral("Missing Help button: %1").arg(dialogName)));

            dialog->show();
            const QString helpText = clickHelpButtonAndCaptureText(helpButton);
            QVERIFY2(!helpText.trimmed().isEmpty(), qPrintable(QStringLiteral("Empty Help content: %1").arg(dialogName)));
            QVERIFY2(
                helpText.contains(expectation.second),
                qPrintable(QStringLiteral("Unexpected Help content for %1: expected \"%2\"").arg(dialogName, expectation.second)));
            dialog->close();
        }
    }

    void writesManualDialogInspectionImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_DIALOG_GALLERY_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_DIALOG_GALLERY_DIR to write dialog PNGs for manual inspection.");
        }

        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        const UiBuilder::DialogContext context = populatedContext();
        int written = 0;
        for (const QString& dialogName : UiBuilder::dialogNames()) {
            if (isToolbarResourceName(dialogName)) {
                continue;
            }
            const QString stem = QStringLiteral("%1%2")
                .arg(QString())
                .arg(safeImageStem(dialogName));
            std::unique_ptr<QDialog> initialDialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(initialDialog != nullptr, qPrintable(dialogName));
            QVERIFY2(
                saveDialogImage(initialDialog.get(), outputDirectory, QStringLiteral("%1_%2_initial")
                    .arg(written, 2, 10, QLatin1Char('0'))
                    .arg(stem)),
                qPrintable(QStringLiteral("Could not save initial dialog image for %1").arg(dialogName)));
            initialDialog->close();

            std::unique_ptr<QDialog> wideDialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(wideDialog != nullptr, qPrintable(dialogName));
            applyInterestingDialogState(wideDialog.get());
            QVERIFY2(
                saveDialogImage(wideDialog.get(), outputDirectory, QStringLiteral("%1_%2_wide")
                    .arg(written, 2, 10, QLatin1Char('0'))
                    .arg(stem)),
                qPrintable(QStringLiteral("Could not save wide-state dialog image for %1").arg(dialogName)));
            wideDialog->close();

            if (dialogName == QStringLiteral("SEARCH")) {
                const std::pair<int, QString> variants[] = {
                    {UiIds::Control::SearchCompareAnd, QStringLiteral("and_variant")},
                    {UiIds::Control::SearchCompareOr, QStringLiteral("or_variant")},
                };
                for (const auto& [controlId, suffix] : variants) {
                    std::unique_ptr<QDialog> variantDialog = UiBuilder::createDialog(dialogName, nullptr, context);
                    QVERIFY2(variantDialog != nullptr, qPrintable(dialogName));
                    if (auto* button = qobject_cast<QAbstractButton*>(UiBuilder::controlById(variantDialog.get(), controlId))) {
                        button->click();
                        QCoreApplication::processEvents();
                    }
                    QVERIFY2(
                        saveDialogImage(variantDialog.get(), outputDirectory, QStringLiteral("%1_%2_%3")
                            .arg(written, 2, 10, QLatin1Char('0'))
                            .arg(stem, suffix)),
                        qPrintable(QStringLiteral("Could not save variant dialog image for %1").arg(dialogName)));
                    variantDialog->close();
                }
            }
            ++written;
        }

        QVERIFY(written > 20);
        qInfo("Wrote %d dialog image pairs to %s", written, qPrintable(QFileInfo(outputDirectory.path()).absoluteFilePath()));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runUiBuilderTests(int argc, char** argv)
{
    UiBuilderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(UiBuilderTests)
#endif

#include "UiBuilderTests.moc"

