#include "UiBuilder.h"
#include "UiResourceData.h"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

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
            {QStringLiteral("CALL"), QStringLiteral("operating system phone-link handler")},
            {QStringLiteral("PHNDEF"), QStringLiteral("operating system phone-link handler")},
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

