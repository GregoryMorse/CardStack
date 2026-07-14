#include "MainWindow.h"

#include "../support/ModalDialogDriver.h"
#include "../support/VisualTestSupport.h"

#include <QAbstractButton>
#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QKeySequence>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QScrollBar>
#include <QTest>
#include <QTemporaryDir>
#include <QTableView>
#include <QToolBar>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QListWidget>
#include <QTimer>
#include <QSpinBox>

#include <algorithm>
#include <functional>
#include <memory>

#include "DeckWorkspace.h"
#include "FieldDefinition.h"
#include "ReportDesignerWidget.h"
#include "SQLiteDeckStore.h"
#include "SQLitePackageStore.h"
#include "TemplateDesignerWidget.h"
#include "UiBuilder.h"
#include "UiIds.h"

using namespace CardStack;

namespace {

void collectCommandIdsFromActions(const QList<QAction*>& actions, QSet<int>* commandIds)
{
    for (QAction* action : actions) {
        if (QMenu* menu = action->menu()) {
            collectCommandIdsFromActions(menu->actions(), commandIds);
            continue;
        }

        if (action->data().isValid()) {
            commandIds->insert(action->data().toInt());
        }
    }
}

QSet<int> commandIdsFromMenuBar(QMenuBar* menuBar)
{
    QSet<int> commandIds;
    collectCommandIdsFromActions(menuBar->actions(), &commandIds);
    return commandIds;
}

QStringList topLevelMenuTitles(QMenuBar* menuBar)
{
    QStringList titles;
    for (QAction* action : menuBar->actions()) {
        if (QMenu* menu = action->menu()) {
            QString title = menu->title();
            title.remove(QLatin1Char('&'));
            titles.append(title);
        }
    }
    return titles;
}

QMenu* findTopLevelMenu(QMenuBar* menuBar, const QString& title)
{
    for (QAction* action : menuBar->actions()) {
        QMenu* menu = action->menu();
        if (menu == nullptr) {
            continue;
        }
        QString menuTitle = menu->title();
        menuTitle.remove(QLatin1Char('&'));
        if (menuTitle == title) {
            return menu;
        }
    }
    return nullptr;
}

QSet<int> commandIdsFromToolBar(QToolBar* toolBar)
{
    QSet<int> commandIds;
    for (QAction* action : toolBar->actions()) {
        if (action->data().isValid()) {
            commandIds.insert(action->data().toInt());
        }
    }
    return commandIds;
}

bool saveMainWindowImage(MainWindow& window, const QDir& outputDirectory, const QString& fileName)
{
    window.resize(1040, 720);
    return Tests::saveWidgetImage(window, outputDirectory, fileName);
}

QAction* findCommandAction(const QList<QAction*>& actions, int commandId)
{
    for (QAction* action : actions) {
        if (action->data().toInt() == commandId) {
            return action;
        }

        if (QMenu* menu = action->menu()) {
            if (QAction* found = findCommandAction(menu->actions(), commandId)) {
                return found;
            }
        }
    }

    return nullptr;
}

QAction* findCommandAction(QMenuBar* menuBar, int commandId)
{
    return findCommandAction(menuBar->actions(), commandId);
}

void acceptNextNewFileDialog(int sourceIndex)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [sourceIndex, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->property("legacyDialogName").toString() != QStringLiteral("NEWFILE")) {
                continue;
            }

            auto* sourceCombo = qobject_cast<QComboBox*>(CardStack::UiBuilder::controlById(dialog, UiIds::Control::NewFileSourceCombo));
            auto* templateList = qobject_cast<QListWidget*>(CardStack::UiBuilder::controlById(dialog, UiIds::Control::NewFileTemplateList));
            if (sourceCombo != nullptr) {
                sourceCombo->setCurrentIndex(sourceIndex);
            }
            if (templateList != nullptr && templateList->count() > 0) {
                templateList->setCurrentRow(0);
            }
            dialog->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void rejectNextDialogWithTitle(const QString& title, bool* seen)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [title, seen, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->windowTitle() != title) {
                continue;
            }

            if (seen != nullptr) {
                *seen = true;
            }
            dialog->reject();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextDialogByLegacyName(const QString& dialogName)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [dialogName, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->property("legacyDialogName").toString() != dialogName) {
                continue;
            }

            dialog->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextImportReviewDialog()
{
    auto attemptsRemaining = std::make_shared<int>(200);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() ||
                dialog->objectName() != QStringLiteral("importExamineDialog")) {
                continue;
            }
            dialog->accept();
            return;
        }
        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };
    QTimer::singleShot(0, qApp, *retry);
}

void captureNextAboutDialog(const QDir& outputDirectory, bool* saved)
{
    auto attemptsRemaining = std::make_shared<int>(100);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [outputDirectory, saved, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->windowTitle() != QStringLiteral("About CardStack")) {
                continue;
            }

            *saved = Tests::saveWidgetImage(*dialog, outputDirectory, QStringLiteral("app_about_dialog.png"));
            dialog->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void handleNextDialogByLegacyName(
    const QString& dialogName,
    const std::function<void(QDialog*)>& handler)
{
    auto attemptsRemaining = std::make_shared<int>(80);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [dialogName, handler, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->property("legacyDialogName").toString() != dialogName) {
                continue;
            }

            handler(dialog);
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextPasswordDialog(const QString& password, bool* seen = nullptr)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [password, seen, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->property("legacyDialogName").toString() != QStringLiteral("GETPASSWORD")) {
                continue;
            }

            if (auto* passwordEdit = qobject_cast<QLineEdit*>(CardStack::UiBuilder::controlById(dialog, UiIds::Control::SecurityPassword))) {
                passwordEdit->setText(password);
            }
            if (seen != nullptr) {
                *seen = true;
            }
            dialog->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextReportFormDialog(ReportFormType formType, int row)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [formType, row, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->property("legacyDialogName").toString() != QStringLiteral("REPORTFORM")) {
                continue;
            }

            int radioControl = UiIds::Control::ReportFormReport;
            if (formType == ReportFormType::Card) {
                radioControl = UiIds::Control::ReportFormCard;
            } else if (formType == ReportFormType::Label) {
                radioControl = UiIds::Control::ReportFormLabel;
            }
            if (auto* radio = qobject_cast<QAbstractButton*>(CardStack::UiBuilder::controlById(dialog, radioControl))) {
                radio->click();
            }
            if (auto* list = qobject_cast<QListWidget*>(CardStack::UiBuilder::controlById(dialog, UiIds::Control::ReportFormList))) {
                if (list->count() > 0) {
                    list->setCurrentRow(std::clamp(row, 0, list->count() - 1));
                }
            }
            dialog->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextFileDialogWithFile(const QString& objectName, const QString& filePath)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [objectName, filePath, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->objectName() != objectName) {
                continue;
            }

            dialog->selectFile(filePath);
            static_cast<QDialog*>(dialog)->accept();
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextOpenDialogWithFile(const QString& filePath)
{
    acceptNextFileDialogWithFile(QStringLiteral("cardstackOpenDialog"), filePath);
}

void acceptNextSaveDeckDialogWithFile(const QString& filePath)
{
    acceptNextFileDialogWithFile(QStringLiteral("cardstackSaveDeckDialog"), filePath);
}

void acceptNextMergeSourceDialogWithFile(const QString& filePath)
{
    acceptNextFileDialogWithFile(QStringLiteral("cardstackMergeSourceDialog"), filePath);
}

void acceptNextExportTemplateDialogWithFile(const QString& filePath)
{
    acceptNextFileDialogWithFile(QStringLiteral("cardstackExportTemplateDialog"), filePath);
}

void acceptNextExportReportDialogWithFile(const QString& filePath)
{
    acceptNextFileDialogWithFile(QStringLiteral("cardstackExportReportDialog"), filePath);
}

Deck makeOpenFixtureDeck()
{
    Deck deck(QStringLiteral("Opened Deck"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 4096));
    deck.addCard(CardRecord({QStringLiteral("Imported through File Open"), QStringLiteral("GUI route")}));
    return deck;
}

Deck makeMergeFixtureDeck()
{
    Deck deck(QStringLiteral("Merge Source"));
    deck.addField(FieldDefinition(QStringLiteral("Project"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Status"), FieldType::Text, 32));
    deck.addField(FieldDefinition(QStringLiteral("Lead"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Website"), FieldType::Text, 160));
    deck.addField(FieldDefinition(QStringLiteral("Next step"), FieldType::Text, 160));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 4096));
    deck.addCard(CardRecord({
        QStringLiteral("Merged Project"),
        QStringLiteral("Active"),
        QStringLiteral("Merge Tester"),
        QStringLiteral("https://example.invalid/merged"),
        QStringLiteral("Verify GUI merge route"),
        QStringLiteral("Created by the message-driven action test."),
    }));
    return deck;
}

} // namespace

class MainWindowActionTests : public QObject {
    Q_OBJECT

private slots:
    void resolvesPersistedRoleFontsForFontDialogs()
    {
        const QFont stored = MainWindow::fontForDeckRole(
            QStringLiteral("Arial,-1,13,5,400,0,0,0,0,0"));
        QCOMPARE(stored.family(), QStringLiteral("Arial"));
        QCOMPARE(stored.pixelSize(), 13);

        const QFont fallback = MainWindow::fontForDeckRole(QStringLiteral("not-a-font"));
        QCOMPARE(fallback.family(), QStringLiteral("Arial"));
    }

    void exposesStartupMenuCommands()
    {
        MainWindow window(nullptr, false);
        const QSet<int> commandIds = commandIdsFromMenuBar(window.menuBar());
        QCOMPARE(topLevelMenuTitles(window.menuBar()), QStringList({QStringLiteral("File"), QStringLiteral("Configure"), QStringLiteral("Phone"), QStringLiteral("Help")}));

        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::FilePrinterSetup));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureShowButtonBar));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureEnterWorksLikeTab));
        QVERIFY(commandIds.contains(UiIds::Command::PhoneDial));
        QVERIFY(commandIds.contains(UiIds::Command::HelpContents));
        QVERIFY(commandIds.contains(UiIds::Command::HelpAbout));
        QVERIFY(!commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(!commandIds.contains(UiIds::Command::CardAdd));
    }

    void exposesMainDeckMenuCommands()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();
        const QSet<int> commandIds = commandIdsFromMenuBar(window.menuBar());

        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(commandIds.contains(UiIds::Command::FileMerge));
        QVERIFY(commandIds.contains(UiIds::Command::FilePrintReport));
        QVERIFY(commandIds.contains(UiIds::Command::SearchFind));
        QVERIFY(commandIds.contains(UiIds::Command::ConfigureAddSecurity));
        QVERIFY(commandIds.contains(UiIds::Command::WindowCascade));
        QVERIFY(commandIds.contains(UiIds::Command::HelpAbout));
        QVERIFY(topLevelMenuTitles(window.menuBar()).contains(QStringLiteral("Window")));
    }

    void windowMenuListsMdiChildrenWithActiveCheckmark()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QTRY_VERIFY(mdiArea->subWindowList().size() >= 2);

        QMenu* windowMenu = findTopLevelMenu(window.menuBar(), QStringLiteral("Window"));
        QVERIFY(windowMenu != nullptr);

        QList<QAction*> windowActions;
        for (QAction* action : windowMenu->actions()) {
            if (action->property("cardstackDynamicWindowAction").toBool() && !action->isSeparator()) {
                windowActions.append(action);
            }
        }
        QVERIFY(windowActions.size() >= 2);
        for (QAction* action : windowActions) {
            QVERIFY(action->isCheckable());
            QVERIFY(!action->text().contains(QStringLiteral("Window ")));
        }

        const int checkedCount = std::count_if(windowActions.cbegin(), windowActions.cend(), [](const QAction* action) {
            return action->isChecked();
        });
        QCOMPARE(checkedCount, 1);
    }

    void exposesPhoneDialerConfigurationCommand()
    {
        MainWindow window(nullptr, false);
        auto* phoneDialerAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigurePhoneDialer);
        QVERIFY(phoneDialerAction != nullptr);
        QVERIFY(phoneDialerAction->isVisible());
        QVERIFY(phoneDialerAction->isEnabled());
    }

    void initializesShowButtonBarMenuStateAndTracksAllButtonBars()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* showButtonBarAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigureShowButtonBar);
        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        auto* indexBar = window.findChild<QToolBar*>(QStringLiteral("indexBar"));
        QVERIFY(showButtonBarAction != nullptr);
        QVERIFY(toolBar != nullptr);
        QVERIFY(indexBar != nullptr);

        QVERIFY(showButtonBarAction->isCheckable());
        QVERIFY(showButtonBarAction->isChecked());
        QVERIFY(toolBar->isVisible());
        QVERIFY(indexBar->isVisible());

        showButtonBarAction->trigger();
        QCoreApplication::processEvents();
        QVERIFY(!showButtonBarAction->isChecked());
        QVERIFY(!toolBar->isVisible());
        QVERIFY(!indexBar->isVisible());

        showButtonBarAction->trigger();
        QCoreApplication::processEvents();
        QVERIFY(showButtonBarAction->isChecked());
        QVERIFY(toolBar->isVisible());
        QVERIFY(indexBar->isVisible());
    }

    void exposesStartupToolbarCommands()
    {
        MainWindow window(nullptr, false);
        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        QCOMPARE(toolBar->toolButtonStyle(), Qt::ToolButtonIconOnly);

        const QSet<int> commandIds = commandIdsFromToolBar(toolBar);
        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::PhoneDial));
        QVERIFY(!commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(!commandIds.contains(UiIds::Command::CardAdd));
        QVERIFY(!commandIds.contains(UiIds::Command::ToolAddText));

        for (QAction* action : toolBar->actions()) {
            if (action->isSeparator() || !action->data().isValid()) {
                continue;
            }
            QVERIFY2(!action->icon().isNull(), qPrintable(QStringLiteral("Missing toolbar icon for command %1").arg(action->data().toInt())));
        }
    }

    void exposesDeckToolbarCommands()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        const QSet<int> commandIds = commandIdsFromToolBar(toolBar);

        QVERIFY(commandIds.contains(UiIds::Command::FileNew));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpen));
        QVERIFY(commandIds.contains(UiIds::Command::FileSave));
        QVERIFY(commandIds.contains(UiIds::Command::FilePrintReport));
        QVERIFY(commandIds.contains(UiIds::Command::SearchFind));
        QVERIFY(commandIds.contains(UiIds::Command::SearchFindNext));
        QVERIFY(commandIds.contains(UiIds::Command::SearchReplace));
        QVERIFY(commandIds.contains(UiIds::Command::PhoneDial));
        QVERIFY(commandIds.contains(UiIds::Command::CardAdd));
        QVERIFY(commandIds.contains(UiIds::Command::CardDelete));
        QVERIFY(commandIds.contains(UiIds::Command::NavigateFirstCard));
        QVERIFY(commandIds.contains(UiIds::Command::NavigateLastCard));
        QVERIFY(commandIds.contains(UiIds::Command::FileNewReport));
        QVERIFY(commandIds.contains(UiIds::Command::FileOpenReport));
        QVERIFY(!commandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(!commandIds.contains(UiIds::Command::ToolAddSystemData));

        QAction* findNextAction = findCommandAction(window.menuBar(), UiIds::Command::SearchFindNext);
        QAction* phoneDialAction = findCommandAction(window.menuBar(), UiIds::Command::PhoneDial);
        QVERIFY(findNextAction != nullptr);
        QVERIFY(phoneDialAction != nullptr);
        QVERIFY(!findNextAction->isEnabled());
        QVERIFY(phoneDialAction->isEnabled());

        for (QAction* action : toolBar->actions()) {
            if (action->isSeparator() || !action->data().isValid()) {
                continue;
            }
            QVERIFY2(!action->icon().isNull(), qPrintable(QStringLiteral("Missing toolbar icon for command %1").arg(action->data().toInt())));
        }

        auto* cardPosition = toolBar->findChild<QLabel*>(QStringLiteral("toolbarCardPositionLabel"));
        QVERIFY(cardPosition != nullptr);
        QCOMPARE(cardPosition->text(), QStringLiteral("Card 1 of 2"));
        auto* deckMode = toolBar->findChild<QLabel*>(QStringLiteral("toolbarDeckModeLabel"));
        QVERIFY(deckMode != nullptr);
        QCOMPARE(deckMode->text(), QStringLiteral("Card View"));
    }

    void indexBarShrinksLikeLegacyButtonBarWithoutConstrainingMainWindow()
    {
        MainWindow window(nullptr, true);
        window.resize(1040, 700);
        window.show();
        QCoreApplication::processEvents();

        auto* indexBar = window.findChild<QToolBar*>(QStringLiteral("indexBar"));
        auto* container = window.findChild<QWidget*>(QStringLiteral("indexBarContainer"));
        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(indexBar != nullptr);
        QVERIFY(container != nullptr);
        QVERIFY(mdiArea != nullptr);

        const QList<QPushButton*> buttons =
            container->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(buttons.size(), 37);
        QVERIFY(container->width() > 0);
        QVERIFY(!buttons.first()->visibleRegion().isEmpty());
        QVERIFY(!buttons.last()->visibleRegion().isEmpty());
        QVERIFY(buttons.last()->geometry().right() < container->width());
        int widestIndexGlyphWidth = 1;
        for (const QPushButton* button : buttons) {
            if (!button->text().isEmpty()) {
                widestIndexGlyphWidth = std::max(
                    widestIndexGlyphWidth,
                    button->fontMetrics().horizontalAdvance(button->text()));
            }
        }
        const int wideButtonWidth = buttons.first()->width();

        QTest::keyClick(&window, Qt::Key_F9);
        QCoreApplication::processEvents();
        QVERIFY(indexBar->isVisible());
        QVERIFY(!buttons.first()->visibleRegion().isEmpty());
        QVERIFY(!buttons.last()->visibleRegion().isEmpty());

        QTest::keyClick(&window, Qt::Key_F9);
        QCoreApplication::processEvents();

        window.resize(430, 420);
        QCoreApplication::processEvents();

        QVERIFY(window.width() <= 430);
        QVERIFY(buttons.first()->width() < wideButtonWidth);
        for (const QPushButton* button : buttons) {
            QVERIFY(button->width() >= widestIndexGlyphWidth + 2);
        }
        QCOMPARE(mdiArea->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        QCOMPARE(mdiArea->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);

        const int indexOverflow = container->property("indexContentWidth").toInt() - container->width();
        if (indexOverflow > 0) {
            QScrollBar* horizontalScrollBar = mdiArea->horizontalScrollBar();
            QTRY_VERIFY(horizontalScrollBar->maximum() > horizontalScrollBar->minimum());
            horizontalScrollBar->setValue(horizontalScrollBar->maximum());
            QCoreApplication::processEvents();
            QVERIFY(!buttons.last()->visibleRegion().isEmpty());

            horizontalScrollBar->setValue(horizontalScrollBar->minimum());
            QCoreApplication::processEvents();
            QVERIFY(!buttons.first()->visibleRegion().isEmpty());
        }
    }

    void deckViewShortcutsAndCheckmarksFollowActiveView()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);

        auto* viewCardAction = findCommandAction(window.menuBar(), UiIds::Command::ViewCard);
        auto* viewTableAction = findCommandAction(window.menuBar(), UiIds::Command::ViewTable);
        auto* previousWindowfulAction = findCommandAction(window.menuBar(), UiIds::Command::NavigatePreviousWindowful);
        auto* previousAction = findCommandAction(window.menuBar(), UiIds::Command::NavigatePreviousCard);
        auto* nextAction = findCommandAction(window.menuBar(), UiIds::Command::NavigateNextCard);
        auto* nextWindowfulAction = findCommandAction(window.menuBar(), UiIds::Command::NavigateNextWindowful);
        auto* deckMode = window.findChild<QLabel*>(QStringLiteral("toolbarDeckModeLabel"));
        QVERIFY(viewCardAction != nullptr);
        QVERIFY(viewTableAction != nullptr);
        QVERIFY(previousWindowfulAction != nullptr);
        QVERIFY(previousAction != nullptr);
        QVERIFY(nextAction != nullptr);
        QVERIFY(nextWindowfulAction != nullptr);

        const auto verifyMenuShortcuts = [](int menuId,
                                            std::initializer_list<std::pair<int, QKeySequence>> expected) {
            QMenuBar menuBar;
            QVERIFY(UiBuilder::populateMenuBar(&menuBar, menuId, &menuBar, {}));
            for (const auto& [commandId, shortcut] : expected) {
                QAction* action = findCommandAction(&menuBar, commandId);
                QVERIFY2(action != nullptr, qPrintable(QStringLiteral("Missing command %1 in menu %2").arg(commandId).arg(menuId)));
                QCOMPARE(action->shortcut(), shortcut);
            }
        };
        verifyMenuShortcuts(UiIds::Menu::Startup, {
            {UiIds::Command::FileOpen, QKeySequence(Qt::CTRL | Qt::Key_O)},
            {UiIds::Command::FileExit, QKeySequence(Qt::ALT | Qt::Key_F4)},
            {UiIds::Command::PhoneDial, QKeySequence(Qt::Key_F5)},
            {UiIds::Command::HelpContents, QKeySequence(Qt::Key_F1)},
        });
        verifyMenuShortcuts(UiIds::Menu::MainDeck, {
            {UiIds::Command::FileOpen, QKeySequence(Qt::CTRL | Qt::Key_O)},
            {UiIds::Command::FileSave, QKeySequence(Qt::CTRL | Qt::Key_S)},
            {UiIds::Command::FilePrintReport, QKeySequence(Qt::CTRL | Qt::Key_P)},
            {UiIds::Command::FileExit, QKeySequence(Qt::ALT | Qt::Key_F4)},
            {UiIds::Command::EditUndo, QKeySequence(Qt::CTRL | Qt::Key_Z)},
            {UiIds::Command::EditCut, QKeySequence(Qt::CTRL | Qt::Key_X)},
            {UiIds::Command::EditCopy, QKeySequence(Qt::CTRL | Qt::Key_C)},
            {UiIds::Command::EditPaste, QKeySequence(Qt::CTRL | Qt::Key_V)},
            {UiIds::Command::EditSmartPaste, QKeySequence(Qt::CTRL | Qt::Key_W)},
            {UiIds::Command::EditClear, QKeySequence(Qt::Key_Delete)},
            {UiIds::Command::CardAdd, QKeySequence(Qt::CTRL | Qt::Key_A)},
            {UiIds::Command::CardDelete, QKeySequence(Qt::CTRL | Qt::Key_D)},
            {UiIds::Command::CardDuplicate, QKeySequence(Qt::CTRL | Qt::Key_U)},
            {UiIds::Command::CardUndelete, QKeySequence(Qt::ALT | Qt::Key_F7)},
            {UiIds::Command::ViewCard, QKeySequence(Qt::Key_F9)},
            {UiIds::Command::ViewTable, QKeySequence(Qt::Key_F9)},
            {UiIds::Command::SearchFind, QKeySequence(Qt::CTRL | Qt::Key_F)},
            {UiIds::Command::SearchFindNext, QKeySequence(Qt::CTRL | Qt::Key_N)},
            {UiIds::Command::SearchReplace, QKeySequence(Qt::CTRL | Qt::Key_R)},
            {UiIds::Command::NavigateFirstCard, QKeySequence(Qt::CTRL | Qt::Key_Home)},
            {UiIds::Command::NavigateLastCard, QKeySequence(Qt::CTRL | Qt::Key_End)},
            {UiIds::Command::ConfigureIndex, QKeySequence(Qt::CTRL | Qt::Key_I)},
            {UiIds::Command::PhoneDial, QKeySequence(Qt::Key_F5)},
            {UiIds::Command::WindowTileVertical, QKeySequence(Qt::SHIFT | Qt::Key_F5)},
            {UiIds::Command::WindowTileHorizontal, QKeySequence(Qt::SHIFT | Qt::Key_F4)},
            {UiIds::Command::HelpContents, QKeySequence(Qt::Key_F1)},
        });
        verifyMenuShortcuts(UiIds::Menu::TemplateDesigner, {
            {UiIds::Command::FileOpen, QKeySequence(Qt::CTRL | Qt::Key_O)},
            {UiIds::Command::FileSave, QKeySequence(Qt::CTRL | Qt::Key_S)},
            {UiIds::Command::FileExit, QKeySequence(Qt::ALT | Qt::Key_F4)},
            {UiIds::Command::EditUndo, QKeySequence(Qt::CTRL | Qt::Key_Z)},
            {UiIds::Command::EditCut, QKeySequence(Qt::CTRL | Qt::Key_X)},
            {UiIds::Command::EditCopy, QKeySequence(Qt::CTRL | Qt::Key_C)},
            {UiIds::Command::EditPaste, QKeySequence(Qt::CTRL | Qt::Key_V)},
            {UiIds::Command::EditClear, QKeySequence(Qt::Key_Delete)},
            {UiIds::Command::ToolAddText, QKeySequence(Qt::CTRL | Qt::Key_T)},
            {UiIds::Command::ToolAddDataBox, QKeySequence(Qt::CTRL | Qt::Key_D)},
            {UiIds::Command::ToolAddNotesBox, QKeySequence(Qt::CTRL | Qt::Key_N)},
        });
        verifyMenuShortcuts(UiIds::Menu::ReportDesigner, {
            {UiIds::Command::FileOpen, QKeySequence(Qt::CTRL | Qt::Key_O)},
            {UiIds::Command::FileSaveReport, QKeySequence(Qt::CTRL | Qt::Key_S)},
            {UiIds::Command::FilePrintReport, QKeySequence(Qt::CTRL | Qt::Key_P)},
            {UiIds::Command::FileExit, QKeySequence(Qt::ALT | Qt::Key_F4)},
            {UiIds::Command::EditUndo, QKeySequence(Qt::CTRL | Qt::Key_Z)},
            {UiIds::Command::EditCut, QKeySequence(Qt::CTRL | Qt::Key_X)},
            {UiIds::Command::EditCopy, QKeySequence(Qt::CTRL | Qt::Key_C)},
            {UiIds::Command::EditPaste, QKeySequence(Qt::CTRL | Qt::Key_V)},
            {UiIds::Command::EditClear, QKeySequence(Qt::Key_Delete)},
            {UiIds::Command::ToolAddDataBox, QKeySequence(Qt::CTRL | Qt::Key_D)},
            {UiIds::Command::ToolAddText, QKeySequence(Qt::CTRL | Qt::Key_T)},
            {UiIds::Command::ToolAddLineOrBox, QKeySequence(Qt::CTRL | Qt::Key_L)},
            {UiIds::Command::ToolChangeForm, QKeySequence(Qt::CTRL | Qt::Key_F)},
        });

        for (const QString& dialogName : UiBuilder::dialogNames()) {
            std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(dialogName);
            QVERIFY2(dialog != nullptr, qPrintable(QStringLiteral("Could not construct dialog %1").arg(dialogName)));
            for (QLabel* label : dialog->findChildren<QLabel*>()) {
                if (!QKeySequence::mnemonic(label->text()).isEmpty()) {
                    QVERIFY2(label->buddy() != nullptr,
                             qPrintable(QStringLiteral("Dialog %1 has an unbound mnemonic label: %2")
                                            .arg(dialogName, label->text())));
                }
            }
            for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>()) {
                if (button->text().contains(QLatin1Char('&'))) {
                    QVERIFY2(!QKeySequence::mnemonic(button->text()).isEmpty(),
                             qPrintable(QStringLiteral("Dialog %1 has an invalid button mnemonic: %2")
                                            .arg(dialogName, button->text())));
                }
            }
        }

        QCOMPARE(previousAction->shortcut(), QKeySequence(Qt::Key_PageUp));
        QCOMPARE(nextAction->shortcut(), QKeySequence(Qt::Key_PageDown));
        QCOMPARE(previousWindowfulAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_PageUp));
        QCOMPARE(nextWindowfulAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_PageDown));

        viewTableAction->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(previousAction->shortcut(), QKeySequence(Qt::Key_Up));
        QCOMPARE(nextAction->shortcut(), QKeySequence(Qt::Key_Down));
        QCOMPARE(previousWindowfulAction->shortcut(), QKeySequence(Qt::Key_PageUp));
        QCOMPARE(nextWindowfulAction->shortcut(), QKeySequence(Qt::Key_PageDown));

        viewCardAction->trigger();
        QCoreApplication::processEvents();
        QVERIFY(deckMode != nullptr);
        QCOMPARE(workspace->viewMode(), DeckWorkspace::ViewMode::Card);
        QVERIFY(viewCardAction->isChecked());
        QVERIFY(!viewTableAction->isChecked());
        QCOMPARE(deckMode->text(), QStringLiteral("Card View"));
        QVERIFY(!previousAction->isEnabled());
        QVERIFY(nextAction->isEnabled());

        QTest::keyClick(&window, Qt::Key_F9);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->viewMode(), DeckWorkspace::ViewMode::Table);
        QVERIFY(viewTableAction->isChecked());
        QVERIFY(!viewCardAction->isChecked());
        QCOMPARE(deckMode->text(), QStringLiteral("Table View"));

        QTest::keyClick(&window, Qt::Key_F9);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->viewMode(), DeckWorkspace::ViewMode::Card);
        QVERIFY(viewCardAction->isChecked());
        QVERIFY(!viewTableAction->isChecked());
        QCOMPARE(deckMode->text(), QStringLiteral("Card View"));

        QTest::keyClick(&window, Qt::Key_PageDown);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->currentCardIndex(), 1);
        QVERIFY(previousAction->isEnabled());
        QVERIFY(!nextAction->isEnabled());

        QTest::keyClick(&window, Qt::Key_PageUp);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->currentCardIndex(), 0);
        QVERIFY(!previousAction->isEnabled());
        QVERIFY(nextAction->isEnabled());

        QTest::keyClick(&window, Qt::Key_PageDown, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->currentCardIndex(), 1);

        QTest::keyClick(&window, Qt::Key_PageUp, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(workspace->currentCardIndex(), 0);
    }

    void windowfulMovesByTheVisibleTablePage()
    {
        MainWindow window(nullptr, true);
        window.resize(900, 520);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        for (int index = 0; index < 48; ++index) {
            workspace->addCard();
        }
        workspace->firstCard();

        QAction* tableAction = findCommandAction(window.menuBar(), UiIds::Command::ViewTable);
        QAction* nextWindowful = findCommandAction(window.menuBar(), UiIds::Command::NavigateNextWindowful);
        QAction* previousWindowful = findCommandAction(window.menuBar(), UiIds::Command::NavigatePreviousWindowful);
        QVERIFY(tableAction != nullptr);
        QVERIFY(nextWindowful != nullptr);
        QVERIFY(previousWindowful != nullptr);
        tableAction->trigger();
        QCoreApplication::processEvents();

        auto* table = workspace->findChild<QTableView*>(QStringLiteral("deckTableView"));
        QVERIFY(table != nullptr);
        int visibleRows = 1;
        if (table->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
            visibleRows = table->verticalScrollBar()->pageStep();
        } else {
            int firstRow = table->rowAt(0);
            int lastRow = table->rowAt(table->viewport()->height() - 1);
            firstRow = firstRow < 0 ? 0 : firstRow;
            lastRow = lastRow < 0 ? firstRow : lastRow;
            visibleRows = lastRow - firstRow + 1;
        }
        visibleRows = std::clamp(visibleRows, 1, 31);
        QVERIFY(visibleRows > 1);

        nextWindowful->trigger();
        QCOMPARE(workspace->currentCardIndex(), visibleRows);
        previousWindowful->trigger();
        QCOMPARE(workspace->currentCardIndex(), 0);
    }

    void initializesCommandEnabledStateWithoutDeck()
    {
        MainWindow window(nullptr, false);

        auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        QVERIFY(newAction->isEnabled());

        auto* saveAction = findCommandAction(window.menuBar(), UiIds::Command::FileSave);
        QVERIFY(saveAction == nullptr);

        auto* cardAddAction = findCommandAction(window.menuBar(), UiIds::Command::CardAdd);
        QVERIFY(cardAddAction == nullptr);

        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        auto* saveToolAction = findCommandAction(toolBar->actions(), UiIds::Command::FileSave);
        QVERIFY(saveToolAction == nullptr);
        auto* addTextToolAction = findCommandAction(toolBar->actions(), UiIds::Command::ToolAddText);
        QVERIFY(addTextToolAction == nullptr);
    }

    void triggersShellActionsWithoutKeyboardOrMouse()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        QVERIFY(toolBar->isVisible());

        auto* toggleButtonBar = findCommandAction(window.menuBar(), UiIds::Command::ConfigureShowButtonBar);
        QVERIFY(toggleButtonBar != nullptr);
        toggleButtonBar->trigger();
        QCoreApplication::processEvents();
        QVERIFY(!toolBar->isVisible());

        toggleButtonBar->trigger();
        QCoreApplication::processEvents();
        QVERIFY(toolBar->isVisible());

        auto* enterWorksLikeTab = findCommandAction(window.menuBar(), UiIds::Command::ConfigureEnterWorksLikeTab);
        QVERIFY(enterWorksLikeTab != nullptr);
        const bool initialChecked = enterWorksLikeTab->isChecked();
        enterWorksLikeTab->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(enterWorksLikeTab->isChecked(), !initialChecked);

        enterWorksLikeTab->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(enterWorksLikeTab->isChecked(), initialChecked);
    }

    void dirtyDeckClosePromptCancelKeepsWindowOpen()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        workspace->markDirty();
        QVERIFY(workspace->isDirty());

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        auto* subWindow = mdiArea->activeSubWindow();
        QVERIFY(subWindow != nullptr);

        Tests::chooseNextMessageBoxButton(QMessageBox::Cancel);
        QVERIFY(!subWindow->close());
        QVERIFY(subWindow->isVisible());
        QVERIFY(workspace->isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(subWindow->close());
    }

    void dirtyDeckClosePromptDiscardClosesWindow()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        workspace->markDirty();
        QVERIFY(workspace->isDirty());

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QPointer<QMdiSubWindow> subWindow = mdiArea->activeSubWindow();
        QVERIFY(subWindow != nullptr);

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(subWindow->close());
        QCoreApplication::processEvents();
        QVERIFY(subWindow.isNull() || !subWindow->isVisible());
    }

    void newDeckDesignOptionsOpenTemplateDesigner()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(1);
        newAction->trigger();
        QCoreApplication::processEvents();
        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QVERIFY(designer != nullptr);
        QVERIFY(window.findChildren<DeckWorkspace*>().isEmpty());

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QPointer<QMdiSubWindow> designerWindow;
        for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
            if (subWindow->widget() == designer) {
                designerWindow = subWindow;
                break;
            }
        }
        QVERIFY(designerWindow != nullptr);

        Tests::chooseNextMessageBoxButton(QMessageBox::Yes);
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();
        QTRY_VERIFY(window.findChild<DeckWorkspace*>() != nullptr);

        acceptNextNewFileDialog(2);
        newAction->trigger();
        QCoreApplication::processEvents();
        QVERIFY(window.findChild<TemplateDesignerWidget*>() != nullptr);
    }

    void closingDirtyTemplateDesignerDiscardsWithoutHeapCorruption()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(1);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QVERIFY(designer != nullptr);
        designer->addTextFrame();
        QVERIFY(designer->isDirty());

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QPointer<QMdiSubWindow> designerWindow;
        for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
            if (subWindow->widget() == designer) {
                designerWindow = subWindow;
                break;
            }
        }
        QVERIFY(designerWindow != nullptr);

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        Tests::chooseNextMessageBoxButton(QMessageBox::No);
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();
        QVERIFY(designerWindow.isNull() || !designerWindow->isVisible());
    }

    void switchesToTemplateDesignerMenu()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(1);
        newAction->trigger();
        QCoreApplication::processEvents();

        QVERIFY(window.findChild<TemplateDesignerWidget*>() != nullptr);
        const QSet<int> commandIds = commandIdsFromMenuBar(window.menuBar());
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddDataBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddNotesBox));
        QVERIFY(commandIds.contains(UiIds::Command::ToolFrameAttributes));
        QVERIFY(!commandIds.contains(UiIds::Command::CardAdd));

        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        const QSet<int> toolbarCommandIds = commandIdsFromToolBar(toolBar);
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::FileSave));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddDataBox));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddNotesBox));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddLineOrBox));
        QVERIFY(!toolbarCommandIds.contains(UiIds::Command::CardAdd));
        QVERIFY(!toolbarCommandIds.contains(UiIds::Command::ToolAddSystemData));
    }

    void fileOpenLoadsModernDeckThroughDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("open-fixture.cardstack"));
        SQLiteDeckStore writer;
        QString error;
        QVERIFY2(writer.open(filePath, &error), qPrintable(error));
        QVERIFY2(writer.saveDeck(makeOpenFixtureDeck(), &error), qPrintable(error));

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* openAction = findCommandAction(window.menuBar(), UiIds::Command::FileOpen);
        QVERIFY(openAction != nullptr);
        QVERIFY(openAction->isEnabled());

        acceptNextOpenDialogWithFile(filePath);
        openAction->trigger();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QTRY_VERIFY(workspace != nullptr);
        QCOMPARE(workspace->property("cardstackFilePath").toString(), filePath);
        QCOMPARE(workspace->deck().name(), QStringLiteral("Opened Deck"));
        QCOMPARE(workspace->deck().cardCount(), 1);
        QCOMPARE(workspace->deck().cardAt(0).valueAt(0), QStringLiteral("Imported through File Open"));
    }

    void fileOpenPromptsForSecuredModernDeck()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Deck securedDeck = makeOpenFixtureDeck();
        securedDeck.setSecurity(QStringLiteral("SECRET"), true);

        const QString filePath = directory.filePath(QStringLiteral("secured.cardstack"));
        SQLiteDeckStore writer;
        QString error;
        QVERIFY2(writer.open(filePath, &error), qPrintable(error));
        QVERIFY2(writer.saveDeck(securedDeck, &error), qPrintable(error));

        Deck loadedDirectly;
        SQLiteDeckStore reader;
        QVERIFY2(reader.open(filePath, &error), qPrintable(error));
        QVERIFY2(reader.loadDeck(&loadedDirectly, &error), qPrintable(error));
        QVERIFY(loadedDirectly.hasSecurity());
        QVERIFY(loadedDirectly.hasEncryptedSecurity());
        QCOMPARE(loadedDirectly.securityPassword(), QStringLiteral("SECRET"));

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* openAction = findCommandAction(window.menuBar(), UiIds::Command::FileOpen);
        QVERIFY(openAction != nullptr);
        QVERIFY(openAction->isEnabled());

        bool passwordSeen = false;
        acceptNextOpenDialogWithFile(filePath);
        acceptNextPasswordDialog(QStringLiteral("secret"), &passwordSeen);
        openAction->trigger();
        QTRY_VERIFY(passwordSeen);

        auto* workspace = window.findChild<DeckWorkspace*>();
        QTRY_VERIFY(workspace != nullptr);
        QCOMPARE(workspace->property("cardstackFilePath").toString(), filePath);
        QCOMPARE(workspace->deck().name(), QStringLiteral("Opened Deck"));
        QVERIFY(workspace->hasSecurity());
        QVERIFY(workspace->hasEncryptedSecurity());
    }

    void fileOpenImportsCsvThroughDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("import.csv"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Name,Notes\nCSV Card,Imported through GUI File Open\n");
        file.close();

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        auto* openAction = findCommandAction(window.menuBar(), UiIds::Command::FileOpen);
        QVERIFY(openAction != nullptr);
        QVERIFY(openAction->isEnabled());

        acceptNextOpenDialogWithFile(filePath);
        acceptNextImportReviewDialog();
        openAction->trigger();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QTRY_VERIFY(workspace != nullptr);
        QCOMPARE(workspace->property("cardstackFilePath").toString(), QString());
        QVERIFY(workspace->isDirty());
        QCOMPARE(workspace->deck().fieldCount(), 2);
        QCOMPARE(workspace->deck().cardCount(), 1);
        QCOMPARE(workspace->deck().cardAt(0).valueAt(0), QStringLiteral("CSV Card"));
        QCOMPARE(workspace->deck().cardAt(0).valueAt(1), QStringLiteral("Imported through GUI File Open"));
    }

    void saveAsPersistsDeckAndBindsPathThroughDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("saved-from-gui.cardstack"));

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        QVERIFY(workspace->property("cardstackFilePath").toString().isEmpty());

        auto* saveAsAction = findCommandAction(window.menuBar(), UiIds::Command::FileSaveAs);
        QVERIFY(saveAsAction != nullptr);
        QVERIFY(saveAsAction->isEnabled());

        acceptNextSaveDeckDialogWithFile(filePath);
        saveAsAction->trigger();
        QCoreApplication::processEvents();

        QCOMPARE(workspace->property("cardstackFilePath").toString(), filePath);
        QVERIFY(!workspace->isDirty());
        QVERIFY(QFile::exists(filePath));
    }

    void mergeImportsCardsThroughDialogs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("merge-source.cardstack"));
        SQLiteDeckStore writer;
        QString error;
        QVERIFY2(writer.open(filePath, &error), qPrintable(error));
        QVERIFY2(writer.saveDeck(makeMergeFixtureDeck(), &error), qPrintable(error));

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        const int initialCardCount = workspace->deck().cardCount();

        auto* mergeAction = findCommandAction(window.menuBar(), UiIds::Command::FileMerge);
        QVERIFY(mergeAction != nullptr);
        QVERIFY(mergeAction->isEnabled());

        acceptNextMergeSourceDialogWithFile(filePath);
        acceptNextDialogByLegacyName(QStringLiteral("MERGEDLG"));
        mergeAction->trigger();

        QTRY_COMPARE(workspace->deck().cardCount(), initialCardCount + 1);
        QCOMPARE(workspace->deck().cardAt(initialCardCount).valueAt(0), QStringLiteral("Merged Project"));
        QVERIFY(workspace->isDirty());
    }

    void redefineCommandOpensTemplateDesignerAndHidesDeckWindow()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);
        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QPointer<QMdiSubWindow> deckWindow;
        for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
            if (subWindow->widget() == workspace) {
                deckWindow = subWindow;
                break;
            }
        }
        QVERIFY(deckWindow != nullptr);
        QVERIFY(deckWindow->isVisible());

        auto* redefineAction = findCommandAction(window.menuBar(), UiIds::Command::FileRedefine);
        QVERIFY(redefineAction != nullptr);
        QVERIFY(redefineAction->isEnabled());

        redefineAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QVERIFY(!deckWindow->isVisible());
    }

    void templateDesignerSavePersistsOwnerDeckWhenPathExists()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* redefineAction = findCommandAction(window.menuBar(), UiIds::Command::FileRedefine);
        QVERIFY(redefineAction != nullptr);
        redefineAction->trigger();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QVERIFY(workspace != nullptr);
        QVERIFY(designer != nullptr);

        const QString filePath = directory.filePath(QStringLiteral("designed.cardstack"));
        workspace->setProperty("cardstackFilePath", filePath);
        designer->addTextFrame();
        designer->save();
        QCoreApplication::processEvents();

        Deck loaded;
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(filePath, &error), qPrintable(error));
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QVERIFY(loaded.cardTemplateLayout().frames.size() >= workspace->deck().cardTemplateLayout().frames.size());
        QCOMPARE(loaded.cardTemplateLayout(), workspace->deck().cardTemplateLayout());
    }

    void templateDesignerSaveAsExportsPackageThroughDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* redefineAction = findCommandAction(window.menuBar(), UiIds::Command::FileRedefine);
        QVERIFY(redefineAction != nullptr);
        redefineAction->trigger();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QVERIFY(workspace != nullptr);
        QVERIFY(designer != nullptr);

        designer->addNotesBoxFrame();
        QCoreApplication::processEvents();
        const int editedFieldIndex = designer->selectedFieldIndex();
        QVERIFY(editedFieldIndex >= 0);
        auto* propertyToolbar = window.findChild<QWidget*>(QStringLiteral("designerPropertyToolbar"));
        auto* nameEdit = window.findChild<QLineEdit*>(QStringLiteral("designerFieldNameEdit"));
        auto* lengthSpin = window.findChild<QSpinBox*>(QStringLiteral("designerFieldLengthSpin"));
        auto* phoneCheck = window.findChild<QCheckBox*>(QStringLiteral("designerFieldPhoneCheck"));
        auto* showNameCheck = window.findChild<QCheckBox*>(QStringLiteral("designerFieldShowNameCheck"));
        QVERIFY(propertyToolbar != nullptr);
        QVERIFY(nameEdit != nullptr);
        QVERIFY(lengthSpin != nullptr);
        QVERIFY(phoneCheck != nullptr);
        QVERIFY(showNameCheck != nullptr);
        nameEdit->setText(QStringLiteral("Toolbar Notes"));
        emit nameEdit->editingFinished();
        lengthSpin->setValue(9000);
        phoneCheck->setChecked(true);
        showNameCheck->setChecked(false);
        QCoreApplication::processEvents();
        QCOMPARE(designer->fieldDefinitions().at(editedFieldIndex).name(), QStringLiteral("Toolbar Notes"));
        QCOMPARE(designer->fieldDefinitions().at(editedFieldIndex).maxLength(), 9000);
        QVERIFY(designer->fieldDefinitions().at(editedFieldIndex).isPhone());
        QVERIFY(!designer->fieldDefinitions().at(editedFieldIndex).showName());
        const QString filePath = directory.filePath(QStringLiteral("exported-template.cstemplate"));

        auto* saveAsAction = findCommandAction(window.menuBar(), UiIds::Command::FileSaveAs);
        QVERIFY(saveAsAction != nullptr);
        QVERIFY(saveAsAction->isEnabled());

        acceptNextExportTemplateDialogWithFile(filePath);
        saveAsAction->trigger();
        QCoreApplication::processEvents();

        Deck loaded;
        QString error;
        QVERIFY2(SQLitePackageStore::loadTemplatePackage(filePath, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name(), workspace->deck().name());
        QCOMPARE(loaded.cardTemplateLayout(), designer->layoutDefinition());
        QCOMPARE(loaded.fieldAt(editedFieldIndex).name(), QStringLiteral("Toolbar Notes"));
        QCOMPARE(loaded.fieldAt(editedFieldIndex).maxLength(), 9000);
        QVERIFY(loaded.fieldAt(editedFieldIndex).isPhone());
        QVERIFY(!loaded.fieldAt(editedFieldIndex).showName());
    }

    void menuAndToolbarFollowDeckReportFocusTransitions()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();
        auto* workspace = window.findChild<DeckWorkspace*>();
        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(workspace != nullptr);
        QVERIFY(mdiArea != nullptr);

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        acceptNextDialogByLegacyName(QStringLiteral("REPORTFORM"));
        newReportAction->trigger();
        QCoreApplication::processEvents();
        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);

        QMdiSubWindow* workspaceWindow = nullptr;
        QMdiSubWindow* designerWindow = nullptr;
        for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
            workspaceWindow = subWindow->widget() == workspace ? subWindow : workspaceWindow;
            designerWindow = subWindow->widget() == designer ? subWindow : designerWindow;
        }
        QVERIFY(workspaceWindow != nullptr);
        QVERIFY(designerWindow != nullptr);
        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);

        mdiArea->setActiveSubWindow(workspaceWindow);
        QCoreApplication::processEvents();
        QVERIFY(commandIdsFromMenuBar(window.menuBar()).contains(UiIds::Command::CardAdd));
        QVERIFY(!commandIdsFromMenuBar(window.menuBar()).contains(UiIds::Command::ToolAddSystemData));
        QVERIFY(commandIdsFromToolBar(toolBar).contains(UiIds::Command::CardAdd));

        mdiArea->setActiveSubWindow(designerWindow);
        QCoreApplication::processEvents();
        QVERIFY(commandIdsFromMenuBar(window.menuBar()).contains(UiIds::Command::ToolAddSystemData));
        QVERIFY(!commandIdsFromMenuBar(window.menuBar()).contains(UiIds::Command::CardAdd));
        QVERIFY(commandIdsFromToolBar(toolBar).contains(UiIds::Command::ToolAddSystemData));
    }

    void newReportUsesSafePageMargins()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        QVERIFY(newReportAction->isEnabled());

        acceptNextReportFormDialog(ReportFormType::Report, 0);
        newReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);

        const ReportDefinition report = designer->report();
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.marginLeft, 0);
        QCOMPARE(report.marginTop, 0);
        QCOMPARE(report.marginRight, 0);
        QCOMPARE(report.marginBottom, 0);
        QCOMPARE(report.paperStyleId, 10553);
        QCOMPARE(report.pageWidth, 8500);
        QCOMPARE(report.pageHeight, 14000);

        const QSet<int> commandIds = commandIdsFromMenuBar(window.menuBar());
        QVERIFY(commandIds.contains(UiIds::Command::FileSaveReport));
        QVERIFY(commandIds.contains(UiIds::Command::FileSaveReportAs));
        QVERIFY(commandIds.contains(UiIds::Command::ToolAddSystemData));
        QVERIFY(commandIds.contains(UiIds::Command::ToolChangeForm));

        auto* toolBar = window.findChild<QToolBar*>(QStringLiteral("buttonBar"));
        QVERIFY(toolBar != nullptr);
        const QSet<int> toolbarCommandIds = commandIdsFromToolBar(toolBar);
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::FileSave));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::FileSaveAs));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddText));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddDataBox));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddSystemData));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolAddLineOrBox));
        QVERIFY(toolbarCommandIds.contains(UiIds::Command::ToolChangeForm));
        QVERIFY(!toolbarCommandIds.contains(UiIds::Command::CardAdd));
        QVERIFY(!toolbarCommandIds.contains(UiIds::Command::ToolAddNotesBox));
    }

    void newReportAppliesRecoveredLegalPreset()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        QVERIFY(newReportAction->isEnabled());

        acceptNextReportFormDialog(ReportFormType::Report, 0);
        newReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);

        const ReportDefinition report = designer->report();
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.formWidth, 8500);
        QCOMPARE(report.formHeight, 14000);
        QCOMPARE(report.rows, 1);
        QCOMPARE(report.columns, 1);
        QCOMPARE(report.paperStyleId, 10553);
        QCOMPARE(report.pageWidth, 8500);
        QCOMPARE(report.pageHeight, 14000);
        QCOMPARE(report.marginLeft, 0);
        QCOMPARE(report.marginTop, 0);
        QCOMPARE(report.marginRight, 0);
        QCOMPARE(report.marginBottom, 0);
    }

    void newReportCustomFormUpdatesDefineFormPreviewState()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        QVERIFY(newReportAction->isEnabled());

        bool sawDefineFormState = false;
        handleNextDialogByLegacyName(QStringLiteral("REPORTFORM"), [](QDialog* dialog) {
            auto* list = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportFormList));
            auto* card = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportFormCard));
            auto* report = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportFormReport));
            auto* customButton = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportFormCustom));
            QVERIFY(list != nullptr);
            QVERIFY(card != nullptr);
            QVERIFY(report != nullptr);
            QVERIFY(customButton != nullptr);
            QCOMPARE(list->count(), 5);
            QCOMPARE(list->currentRow(), 0);
            const QRect fixedListGeometry = list->geometry();
            for (int row = 0; row < list->count(); ++row) {
                QVERIFY(!list->item(row)->data(Qt::AccessibleTextRole).toString().contains(
                    QStringLiteral("User Defined"), Qt::CaseInsensitive));
            }
            QWidget* firstRow = list->itemWidget(list->item(0));
            QVERIFY(firstRow != nullptr);
            QCOMPARE(firstRow->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly).size(), 3);
            QVERIFY(customButton->width() >=
                    customButton->fontMetrics().horizontalAdvance(customButton->text()) + 8);
            card->click();
            QCoreApplication::processEvents();
            QCOMPARE(list->count(), 30);
            QCOMPARE(list->currentRow(), 0);
            QCOMPARE(list->geometry(), fixedListGeometry);
            report->click();
            QCoreApplication::processEvents();
            QCOMPARE(list->count(), 5);
            QCOMPARE(list->currentRow(), 0);
            QCOMPARE(list->geometry(), fixedListGeometry);
            customButton->click();
        });
        handleNextDialogByLegacyName(QStringLiteral("DEFINEFORM"), [&sawDefineFormState](QDialog* dialog) {
            auto* label = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormLabel));
            auto* report = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormReport));
            auto* landscape = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormLandscape));
            auto* pageSize = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormPageSize));
            auto* rows = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormRows));
            auto* columns = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormColumns));
            auto* width = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormWidth));
            auto* height = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormHeight));
            auto* countGroup = qobject_cast<QGroupBox*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormCountGroup));
            auto* widthLabel = qobject_cast<QLabel*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormComputedWidth));
            auto* heightLabel = qobject_cast<QLabel*>(
                UiBuilder::controlById(dialog, UiIds::Control::DefineFormComputedHeight));
            QWidget* sample = UiBuilder::controlById(dialog, UiIds::Control::DefineFormSample);

            QVERIFY(label != nullptr);
            QVERIFY(report != nullptr);
            QVERIFY(landscape != nullptr);
            QVERIFY(pageSize != nullptr);
            QVERIFY(rows != nullptr);
            QVERIFY(columns != nullptr);
            QVERIFY(width != nullptr);
            QVERIFY(height != nullptr);
            QVERIFY(countGroup != nullptr);
            QVERIFY(widthLabel != nullptr);
            QVERIFY(heightLabel != nullptr);
            QVERIFY(sample != nullptr);

            QVERIFY(report->isChecked());
            QVERIFY(rows->isEnabled());
            QVERIFY(columns->isEnabled());
            QCOMPARE(width->text().toDouble(), 8.5);
            QCOMPARE(height->text().toDouble(), 14.0);
            QCOMPARE(pageSize->currentText(), QStringLiteral("Legal"));

            label->click();
            landscape->click();
            rows->setText(QStringLiteral("2"));
            columns->setText(QStringLiteral("3"));
            width->setText(QStringLiteral("4.00"));
            height->setText(QStringLiteral("6.00"));
            emit rows->editingFinished();
            emit columns->editingFinished();
            emit width->editingFinished();
            emit height->editingFinished();
            QCoreApplication::processEvents();

            sawDefineFormState = countGroup->title() == QStringLiteral("Labels per page") &&
                rows->isEnabled() &&
                columns->isEnabled() &&
                widthLabel->text().startsWith(QStringLiteral("Label width (in): 4.00")) &&
                heightLabel->text().startsWith(QStringLiteral("Label height (in): 6.00")) &&
                sample->property("formSample").toBool() &&
                sample->property("formSampleRows").toInt() == 2 &&
                sample->property("formSampleColumns").toInt() == 3 &&
                sample->property("formSampleWidthMils").toInt() == 4000 &&
                sample->property("formSampleHeightMils").toInt() == 6000;
            dialog->accept();
        });

        newReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QVERIFY(sawDefineFormState);

        const ReportDefinition report = designer->report();
        QCOMPARE(report.formType, ReportFormType::Label);
        QCOMPARE(report.formWidth, 6000);
        QCOMPARE(report.formHeight, 4000);
        QCOMPARE(report.rows, 2);
        QCOMPARE(report.columns, 3);
    }

    void reportDesignerSaveAsExportsPackageThroughDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        QVERIFY(newReportAction->isEnabled());

        acceptNextDialogByLegacyName(QStringLiteral("REPORTFORM"));
        newReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);

        designer->addLineBoxFrameShape(ReportLineBoxShape::Box, 0, 0, 0);
        QCoreApplication::processEvents();
        auto* shapeCombo = window.findChild<QComboBox*>(QStringLiteral("designerReportShapeCombo"));
        auto* lineStyleCombo = window.findChild<QComboBox*>(QStringLiteral("designerReportLineStyleCombo"));
        auto* fillCombo = window.findChild<QComboBox*>(QStringLiteral("designerReportFillPatternCombo"));
        auto* radiusSpin = window.findChild<QSpinBox*>(QStringLiteral("designerReportCornerRadiusSpin"));
        QVERIFY(shapeCombo != nullptr);
        QVERIFY(lineStyleCombo != nullptr);
        QVERIFY(fillCombo != nullptr);
        QVERIFY(radiusSpin != nullptr);
        shapeCombo->setCurrentIndex(std::min(1, shapeCombo->count() - 1));
        lineStyleCombo->setCurrentIndex(std::min(2, lineStyleCombo->count() - 1));
        fillCombo->setCurrentIndex(std::min(3, fillCombo->count() - 1));
        radiusSpin->setValue(7);
        QCoreApplication::processEvents();
        const ReportFrameDefinition toolbarFrame = *designer->selectedFrameDefinition();
        QCOMPARE(toolbarFrame.kind, ReportFrameKind::LineOrBox);
        QCOMPARE(toolbarFrame.lineStyle, std::min(2, lineStyleCombo->count() - 1));
        QCOMPARE(toolbarFrame.fillPattern, std::min(3, fillCombo->count() - 1));
        QCOMPARE(toolbarFrame.cornerRadius, 7);

        const QString filePath = directory.filePath(QStringLiteral("exported-report.csreport"));
        auto* saveAsAction = findCommandAction(window.menuBar(), UiIds::Command::FileSaveAs);
        QVERIFY(saveAsAction != nullptr);
        QVERIFY(saveAsAction->isEnabled());

        acceptNextExportReportDialogWithFile(filePath);
        saveAsAction->trigger();
        QCoreApplication::processEvents();

        QVector<ReportDefinition> reports;
        QString packageName;
        QString error;
        QVERIFY2(SQLitePackageStore::loadReportPackage(filePath, &reports, &packageName, &error), qPrintable(error));
        QCOMPARE(reports.size(), 1);
        QCOMPARE(packageName, reports.first().name);
        QCOMPARE(reports.first().marginLeft, 0);
        const ReportFrameDefinition persistedToolbarFrame = reports.first().frames.last();
        QCOMPARE(persistedToolbarFrame.kind, ReportFrameKind::LineOrBox);
        QCOMPARE(persistedToolbarFrame.lineStyle, toolbarFrame.lineStyle);
        QCOMPARE(persistedToolbarFrame.fillPattern, toolbarFrame.fillPattern);
        QCOMPARE(persistedToolbarFrame.cornerRadius, toolbarFrame.cornerRadius);
        QCOMPARE(reports.first().marginTop, 0);
        QCOMPARE(reports.first().marginRight, 0);
        QCOMPARE(reports.first().marginBottom, 0);
    }

    void closingDirtyReportDesignerDiscardsWithoutHeapCorruption()
    {
        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReportAction != nullptr);
        QVERIFY(newReportAction->isEnabled());

        acceptNextDialogByLegacyName(QStringLiteral("REPORTFORM"));
        newReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        designer->addTextFrame();
        QVERIFY(designer->isDirty());

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QPointer<QMdiSubWindow> designerWindow;
        for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
            if (subWindow->widget() == designer) {
                designerWindow = subWindow;
                break;
            }
        }
        QVERIFY(designerWindow != nullptr);

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();
        QVERIFY(designerWindow.isNull() || !designerWindow->isVisible());
    }

    void saveUsesExistingDeckPathWithoutSaveAs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window(nullptr, true);
        window.show();
        QCoreApplication::processEvents();

        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace != nullptr);

        const QString filePath = directory.filePath(QStringLiteral("saved.cardstack"));
        workspace->setProperty("cardstackFilePath", filePath);
        workspace->markDirty();
        QVERIFY(workspace->isDirty());

        auto* saveAction = findCommandAction(window.menuBar(), UiIds::Command::FileSave);
        QVERIFY(saveAction != nullptr);
        QVERIFY(saveAction->isEnabled());
        saveAction->trigger();
        QCoreApplication::processEvents();

        QVERIFY2(QFileInfo::exists(filePath), qPrintable(filePath));
        QCOMPARE(workspace->property("cardstackFilePath").toString(), filePath);
        QVERIFY(!workspace->isDirty());

        Deck loaded;
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(filePath, &error), qPrintable(error));
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QCOMPARE(loaded.cardCount(), workspace->deck().cardCount());
        QCOMPARE(loaded.fieldCount(), workspace->deck().fieldCount());
    }

    void writesManualAppInspectionImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_APP_GALLERY_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_APP_GALLERY_DIR to write app-window PNGs for manual inspection.");
        }

        Tests::installVisualTestFonts();
        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        {
            MainWindow window(nullptr, false);
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_startup_toolbar.png")));
        }

        {
            MainWindow window(nullptr, true);
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_deck_toolbar.png")));
        }

        {
            MainWindow window(nullptr, false);
            window.show();
            QCoreApplication::processEvents();
            auto* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
            QVERIFY(newAction != nullptr);
            acceptNextNewFileDialog(1);
            newAction->trigger();
            QCoreApplication::processEvents();
            QTRY_VERIFY(window.findChild<TemplateDesignerWidget*>() != nullptr);
            auto* designer = window.findChild<TemplateDesignerWidget*>();
            while (designer->selectedFrameIndex() >= 0) {
                designer->deleteSelectedFrame();
            }
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_template_toolbar.png")));
            designer->addTextFrameWithText(QStringLiteral("Heading"));
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_template_toolbar_text.png")));
            designer->deleteSelectedFrame();
            designer->addDataBoxFrameForField(designer->fieldNames().value(0));
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_template_toolbar_data.png")));
            designer->deleteSelectedFrame();
            designer->addNotesBoxFrame();
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_template_toolbar_notes.png")));
            designer->deleteSelectedFrame();
            designer->addLineBoxFrameShape(CardTemplateLineBoxShape::Box, 0, 0, 0);
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_template_toolbar_linebox.png")));
        }

        {
            MainWindow window(nullptr, true);
            window.show();
            QCoreApplication::processEvents();
            auto* newReportAction = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
            QVERIFY(newReportAction != nullptr);
            acceptNextReportFormDialog(ReportFormType::Report, 0);
            newReportAction->trigger();
            QCoreApplication::processEvents();
            QTRY_VERIFY(window.findChild<ReportDesignerWidget*>() != nullptr);
            auto* designer = window.findChild<ReportDesignerWidget*>();
            while (designer->selectedFrameIndex() >= 0) {
                designer->deleteSelectedFrame();
            }
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_report_toolbar.png")));
            designer->addTextFrameWithText(QStringLiteral("Heading"), 0);
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_report_toolbar_text.png")));
            designer->deleteSelectedFrame();
            designer->addDataFrameForField(designer->fieldNames().value(0), 0, true);
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_report_toolbar_data.png")));
            designer->deleteSelectedFrame();
            designer->addSystemFrameWithText(QStringLiteral("Page Number"), 0);
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_report_toolbar_system.png")));
            designer->deleteSelectedFrame();
            designer->addLineBoxFrameShape(ReportLineBoxShape::Box, 0, 0, 0);
            QCoreApplication::processEvents();
            QVERIFY(saveMainWindowImage(window, outputDirectory, QStringLiteral("app_report_toolbar_linebox.png")));
        }

        {
            MainWindow window(nullptr, false);
            window.show();
            QCoreApplication::processEvents();
            auto* aboutAction = findCommandAction(window.menuBar(), UiIds::Command::HelpAbout);
            QVERIFY(aboutAction != nullptr);
            bool saved = false;
            captureNextAboutDialog(outputDirectory, &saved);
            aboutAction->trigger();
            QVERIFY(saved);
        }
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runMainWindowActionTests(int argc, char** argv)
{
    MainWindowActionTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(MainWindowActionTests)
#endif

#include "MainWindowActionTests.moc"

