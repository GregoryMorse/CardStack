#include "UiBuilder.h"
#include "UiResourceData.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
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
            auto* beginning = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog.get(), UiIds::Control::SearchDirectionBeginning));
            QVERIFY(noCompare != nullptr);
            QVERIFY(beginning != nullptr);
            QVERIFY(noCompare->isChecked());
            QVERIFY(beginning->isChecked());
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
        auto* useSystem = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::ColorUseSystem));

        QVERIFY(roleCombo != nullptr);
        QVERIFY(swatchGrid != nullptr);
        QVERIFY(useSystem != nullptr);
        QCOMPARE(roleCombo->count(), UiIds::StringId::ColorRoleLast - UiIds::StringId::ColorRoleFirst + 1);
        QCOMPARE(roleCombo->currentIndex(), 5);
        QVERIFY(useSystem->isCheckable());
#ifdef Q_OS_WIN
        QVERIFY(!useSystem->isHidden());
#else
        QVERIFY(useSystem->isHidden());
#endif
        roleCombo->setCurrentIndex(1);
        useSystem->setChecked(true);
        QCoreApplication::processEvents();
        QCOMPARE(roleCombo->currentIndex(), 1);
        QVERIFY(useSystem->isChecked());
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
            const QString stem = QStringLiteral("%1%2")
                .arg(isToolbarResourceName(dialogName) ? QStringLiteral("toolbar_") : QString())
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

