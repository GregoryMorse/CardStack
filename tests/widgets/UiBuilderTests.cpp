#include "UiBuilder.h"
#include "UiResourceData.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QSpinBox>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

#include <algorithm>
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

    const QString text = qobject_cast<const QAbstractButton*>(widget) != nullptr
        ? qobject_cast<const QAbstractButton*>(widget)->text()
        : QString();
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

bool isContainerDecoration(const QWidget* widget)
{
    return qobject_cast<const QGroupBox*>(widget) != nullptr ||
        widget->property("uiControlClass").toString() == QStringLiteral("static") &&
            widget->geometry().width() <= 4;
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
    if (!first->isVisible() || !second->isVisible()) {
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

    const int firstArea = firstRect.width() * firstRect.height();
    const int secondArea = secondRect.width() * secondRect.height();
    const int smallerArea = std::max(1, std::min(firstArea, secondArea));
    return area > std::max(6, smallerArea / 8);
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

    void everyVisibleDialogControlHasSafeGeometry()
    {
        const UiBuilder::DialogContext context = populatedContext();
        for (const QString& dialogName : UiBuilder::dialogNames()) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName, nullptr, context);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Dialog did not instantiate: %1").arg(dialogName)));

            const QRect safeDialogRect = dialog->rect().adjusted(-1, -1, 1, 1);
            const QList<QWidget*> controls = dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget* widget : controls) {
                if (!widget->isVisible()) {
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
                if (!comboBox->isVisible() || comboBox->count() == 0) {
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
        QVERIFY(quickDials->geometry().top() < 70);
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

