#include "../support/ModalDialogDriver.h"

#include "DeckWorkspace.h"
#include "FieldDefinition.h"
#include "MainWindow.h"
#include "ReportDesignerWidget.h"
#include "SQLiteDeckStore.h"
#include "TemplateDesignerWidget.h"
#include "UiBuilder.h"
#include "UiIds.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>
#include <QtTest/QtTest>

#include <algorithm>
#include <functional>
#include <memory>

using namespace CardStack;

namespace {

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

QAction* findCommandAction(const QMenuBar* menuBar, int commandId)
{
    return menuBar == nullptr ? nullptr : findCommandAction(menuBar->actions(), commandId);
}

QDialog* findVisibleLegacyDialog(const QString& dialogName)
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr
            && dialog->isVisible()
            && dialog->property("legacyDialogName").toString() == dialogName) {
            return dialog;
        }
    }
    return nullptr;
}

void handleNextLegacyDialog(
    const QString& dialogName,
    const std::function<bool(QDialog*)>& handler)
{
    auto attemptsRemaining = std::make_shared<int>(200);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [dialogName, handler, attemptsRemaining, retry]() {
        if (QDialog* dialog = findVisibleLegacyDialog(dialogName)) {
            if (handler(dialog)) {
                return;
            }
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextNewFileDialog(int sourceIndex)
{
    handleNextLegacyDialog(QStringLiteral("NEWFILE"), [sourceIndex](QDialog* dialog) {
        auto* sourceCombo = qobject_cast<QComboBox*>(
            UiBuilder::controlById(dialog, UiIds::Control::NewFileSourceCombo));
        if (sourceCombo != nullptr) {
            sourceCombo->setCurrentIndex(sourceIndex);
        }
        dialog->accept();
        return true;
    });
}

void acceptNextOpenDialogWithFile(const QString& filePath)
{
    auto attemptsRemaining = std::make_shared<int>(80);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [filePath, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->objectName() != QStringLiteral("cardstackOpenDialog")) {
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

void handleNextImportReview(const std::function<bool(QDialog*)>& handler)
{
    auto attemptsRemaining = std::make_shared<int>(200);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [handler, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog != nullptr && dialog->isVisible()
                && dialog->objectName() == QStringLiteral("importExamineDialog")) {
                if (handler(dialog)) {
                    return;
                }
            }
        }
        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };
    QTimer::singleShot(0, qApp, *retry);
}

void acceptNextPatternDeckDialogWithFile(const QString& filePath)
{
    auto attemptsRemaining = std::make_shared<int>(300);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [filePath, attemptsRemaining, retry]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (dialog == nullptr || !dialog->isVisible() || dialog->objectName() != QStringLiteral("cardstackPatternDeckDialog")) {
                continue;
            }

            const QFileInfo fileInfo(filePath);
            dialog->setDirectory(fileInfo.absolutePath());
            dialog->selectFile(fileInfo.fileName());
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

void acceptNextPatternAfterDeckFlow(const QString& filePath)
{
    handleNextLegacyDialog(QStringLiteral("NEWFILE"), [filePath](QDialog* dialog) {
        auto* sourceCombo = qobject_cast<QComboBox*>(
            UiBuilder::controlById(dialog, UiIds::Control::NewFileSourceCombo));
        if (sourceCombo != nullptr) {
            sourceCombo->setCurrentIndex(3);
        }
        acceptNextPatternDeckDialogWithFile(filePath);
        dialog->accept();
        return true;
    });
}

void acceptNextSecurityDialog(const QString& dialogName, const QString& password, bool encrypted = false)
{
    handleNextLegacyDialog(dialogName, [password, encrypted](QDialog* dialog) {
        auto* passwordEdit = qobject_cast<QLineEdit*>(
            UiBuilder::controlById(dialog, UiIds::Control::SecurityPassword));
        if (passwordEdit != nullptr) {
            passwordEdit->setText(password);
        }
        if (auto* encryptButton = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::SecurityEncryptData))) {
            encryptButton->setChecked(encrypted);
        }
        dialog->accept();
        return true;
    });
}

void chooseMessageBoxButtons(QVector<QMessageBox::StandardButton> buttons)
{
    auto pending = std::make_shared<QVector<QMessageBox::StandardButton>>(std::move(buttons));
    auto attemptsRemaining = std::make_shared<int>(300);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [pending, attemptsRemaining, retry]() {
        if (pending->isEmpty()) {
            return;
        }

        if (CardStack::Tests::clickMessageBoxButton(pending->first())) {
            pending->removeFirst();
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0 && !pending->isEmpty()) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

DeckWorkspace* activeWorkspace(MainWindow& window)
{
    auto* mdiArea = window.findChild<QMdiArea*>();
    if (mdiArea == nullptr || mdiArea->activeSubWindow() == nullptr) {
        return nullptr;
    }
    return qobject_cast<DeckWorkspace*>(mdiArea->activeSubWindow()->widget());
}

QMdiSubWindow* subWindowForWidget(MainWindow& window, QWidget* child)
{
    auto* mdiArea = window.findChild<QMdiArea*>();
    if (mdiArea == nullptr || child == nullptr) {
        return nullptr;
    }
    for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
        if (subWindow->widget() == child) {
            return subWindow;
        }
    }
    return nullptr;
}

Deck makeSearchWorkflowDeck()
{
    Deck deck(QStringLiteral("Workflow Deck"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Group"), FieldType::Text, 80));
    deck.addCard(CardRecord({QStringLiteral("Charlie"), QStringLiteral("Blue")}));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Red")}));
    deck.addCard(CardRecord({QStringLiteral("Beta"), QStringLiteral("Red")}));
    return deck;
}

Deck makePhoneWorkflowDeck()
{
    Deck deck(QStringLiteral("Phone Workflow Deck"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Phone"), FieldType::Text, 80));
    deck.addCard(CardRecord({QStringLiteral("Support"), QStringLiteral("555-0100")}));
    return deck;
}

QString writeWorkflowDeck(QTemporaryDir* directory)
{
    const QString filePath = directory->filePath(QStringLiteral("workflow.cardstack"));
    SQLiteDeckStore writer;
    QString error;
    if (!writer.open(filePath, &error) || !writer.saveDeck(makeSearchWorkflowDeck(), &error)) {
        return {};
    }
    return filePath;
}

QString writePhoneWorkflowDeck(QTemporaryDir* directory)
{
    const QString filePath = directory->filePath(QStringLiteral("phone-workflow.cardstack"));
    SQLiteDeckStore writer;
    QString error;
    if (!writer.open(filePath, &error) || !writer.saveDeck(makePhoneWorkflowDeck(), &error)) {
        return {};
    }
    return filePath;
}

DeckWorkspace* openWorkflowDeck(MainWindow* window, const QString& filePath)
{
    QAction* openAction = findCommandAction(window->menuBar(), UiIds::Command::FileOpen);
    if (openAction == nullptr) {
        return nullptr;
    }

    acceptNextOpenDialogWithFile(filePath);
    openAction->trigger();
    QCoreApplication::processEvents();
    return activeWorkspace(*window);
}

void setComboText(QDialog* dialog, int controlId, const QString& text)
{
    auto* combo = qobject_cast<QComboBox*>(UiBuilder::controlById(dialog, controlId));
    if (combo == nullptr) {
        return;
    }
    combo->setEditText(text);
}

void setLineEditText(QDialog* dialog, int controlId, const QString& text)
{
    auto* edit = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog, controlId));
    if (edit != nullptr) {
        edit->setText(text);
    }
}

} // namespace

class MainWindowWorkflowTests : public QObject {
    Q_OBJECT

private slots:
    void closeAllPromptsEachDirtyWindowAndStopsOnCancel()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QTRY_COMPARE(mdiArea->subWindowList().size(), 2);

        QAction* closeAllAction = findCommandAction(window.menuBar(), UiIds::Command::WindowCloseAll);
        QVERIFY(closeAllAction != nullptr);

        chooseMessageBoxButtons({QMessageBox::Cancel});
        closeAllAction->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(mdiArea->subWindowList().size(), 2);

        chooseMessageBoxButtons({QMessageBox::Discard, QMessageBox::Discard});
        closeAllAction->trigger();
        QTRY_COMPARE(mdiArea->subWindowList().size(), 0);
    }

    void exitPromptsEachDirtyWindowAndStopsOnCancel()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QTRY_COMPARE(mdiArea->subWindowList().size(), 2);

        QAction* exitAction = findCommandAction(window.menuBar(), UiIds::Command::FileExit);
        QVERIFY(exitAction != nullptr);

        chooseMessageBoxButtons({QMessageBox::Cancel});
        exitAction->trigger();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QCOMPARE(mdiArea->subWindowList().size(), 2);

        chooseMessageBoxButtons({QMessageBox::Discard, QMessageBox::Discard});
        exitAction->trigger();
        QTRY_VERIFY(!window.isVisible());
    }

    void closeDirtyDeckWithSavePersistsAndClosesWindow()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QTRY_COMPARE(mdiArea->subWindowList().size(), 1);

        QPointer<DeckWorkspace> workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        const QString filePath = directory.filePath(QStringLiteral("close-save.cardstack"));
        workspace->setProperty("cardstackFilePath", filePath);
        workspace->markDirty();
        QVERIFY(workspace->isDirty());

        QPointer<QMdiSubWindow> deckWindow = subWindowForWidget(window, workspace);
        QVERIFY(deckWindow != nullptr);

        QAction* closeAction = findCommandAction(window.menuBar(), UiIds::Command::FileClose);
        QVERIFY(closeAction != nullptr);
        chooseMessageBoxButtons({QMessageBox::Save});
        closeAction->trigger();
        QCoreApplication::processEvents();

        QTRY_VERIFY(deckWindow.isNull() || !deckWindow->isVisible());
        QVERIFY2(QFileInfo::exists(filePath), qPrintable(filePath));

        Deck loaded;
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(filePath, &error), qPrintable(error));
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QVERIFY(loaded.fieldCount() > 0);
        QVERIFY(loaded.cardCount() > 0);
    }

    void closeDeckFromReportHonorsDependentDesignerCancel()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);

        QAction* newReport = findCommandAction(window.menuBar(), UiIds::Command::FileNewReport);
        QVERIFY(newReport != nullptr);
        handleNextLegacyDialog(QStringLiteral("REPORTFORM"), [](QDialog* dialog) {
            dialog->accept();
            return true;
        });
        newReport->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        designer->addTextFrameWithText(QStringLiteral("Unsaved dependent report"), 0);
        QVERIFY(designer->isDirty());
        QTRY_COMPARE(mdiArea->subWindowList().size(), 2);

        QAction* closeDeck = findCommandAction(window.menuBar(), UiIds::Command::FileCloseDeck);
        QVERIFY(closeDeck != nullptr);
        chooseMessageBoxButtons({QMessageBox::Cancel});
        closeDeck->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(mdiArea->subWindowList().size(), 2);
        QVERIFY(designer->isVisible());

        chooseMessageBoxButtons({QMessageBox::Discard, QMessageBox::Discard});
        closeDeck->trigger();
        QTRY_COMPARE(mdiArea->subWindowList().size(), 0);
    }

    void startupRestoresPersistedMainAndDeckWindowSession()
    {
        {
            QSettings settings;
            settings.remove(QStringLiteral("windowSession"));
            settings.sync();
        }

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = writeWorkflowDeck(&directory);
        QVERIFY(!filePath.isEmpty());

        QRect savedDeckGeometry;
        QSize savedMainSize;
        {
            MainWindow window(nullptr, false, false);
            window.resize(780, 690);
            window.show();
            QCoreApplication::processEvents();
            DeckWorkspace* workspace = openWorkflowDeck(&window, filePath);
            QVERIFY(workspace != nullptr);
            QCOMPARE(
                QFileInfo(workspace->property("cardstackFilePath").toString()).absoluteFilePath(),
                QFileInfo(filePath).absoluteFilePath());
            QMdiSubWindow* deckWindow = subWindowForWidget(window, workspace);
            QVERIFY(deckWindow != nullptr);
            deckWindow->showNormal();
            deckWindow->setGeometry(37, 43, 720, 390);
            QVERIFY(!deckWindow->isMinimized());
            QVERIFY(!deckWindow->isMaximized());
            savedDeckGeometry = deckWindow->geometry();
            savedMainSize = window.size();
            QVERIFY(window.close());
        }

        {
            QSettings persisted;
            QCOMPARE(persisted.value(QStringLiteral("windowSession/version")).toInt(), 1);
            const QJsonDocument session = QJsonDocument::fromJson(
                persisted.value(QStringLiteral("windowSession/deckWindows")).toByteArray());
            QVERIFY(session.isArray());
            QCOMPARE(session.array().size(), 1);
        }

        {
            MainWindow restored(nullptr, false, true);
            restored.show();
            QCoreApplication::processEvents();
            auto* mdiArea = restored.findChild<QMdiArea*>();
            QVERIFY(mdiArea != nullptr);
            QTRY_COMPARE(mdiArea->subWindowList().size(), 1);
            auto* workspace = qobject_cast<DeckWorkspace*>(mdiArea->activeSubWindow()->widget());
            QVERIFY(workspace != nullptr);
            QCOMPARE(
                QFileInfo(workspace->property("cardstackFilePath").toString()).absoluteFilePath(),
                QFileInfo(filePath).absoluteFilePath());
            QTRY_COMPARE(mdiArea->activeSubWindow()->geometry(), savedDeckGeometry);
            QCOMPARE(restored.size(), savedMainSize);
            QVERIFY(restored.property("cardstackMainGeometryRestored").toBool());
            QVERIFY(restored.close());
        }

        {
            QSettings settings;
            settings.remove(QStringLiteral("windowSession"));
            settings.sync();
        }
    }

    void windowArrangementCommandsKeepOpenWindowsReachable()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        QTRY_COMPARE(mdiArea->subWindowList().size(), 2);

        const QVector<int> commands = {
            UiIds::Command::WindowCascade,
            UiIds::Command::WindowTileVertical,
            UiIds::Command::WindowTileHorizontal,
        };
        for (int commandId : commands) {
            QAction* action = findCommandAction(window.menuBar(), commandId);
            QVERIFY(action != nullptr);
            QVERIFY(action->isEnabled());
            action->trigger();
            QCoreApplication::processEvents();

            QCOMPARE(mdiArea->subWindowList().size(), 2);
            for (QMdiSubWindow* subWindow : mdiArea->subWindowList()) {
                QVERIFY(subWindow->isVisible());
                QVERIFY(subWindow->geometry().isValid());
                QVERIFY(subWindow->widget() != nullptr);
            }
        }

        const QList<QMdiSubWindow*> windows = mdiArea->subWindowList(QMdiArea::CreationOrder);
        QCOMPARE(windows.size(), 2);
        for (QMdiSubWindow* subWindow : windows) {
            subWindow->showMinimized();
            subWindow->move(32, 32);
        }
        QCoreApplication::processEvents();
        QAction* arrangeIcons = findCommandAction(window.menuBar(), UiIds::Command::WindowArrangeIcons);
        QVERIFY(arrangeIcons != nullptr);
        QVERIFY(arrangeIcons->isEnabled());
        arrangeIcons->trigger();
        QCoreApplication::processEvents();
        QVERIFY(windows.at(0)->isMinimized());
        QVERIFY(windows.at(1)->isMinimized());
        QVERIFY(!windows.at(0)->geometry().intersects(windows.at(1)->geometry()));
        QVERIFY(mdiArea->viewport()->rect().contains(windows.at(0)->geometry()));
        QVERIFY(mdiArea->viewport()->rect().contains(windows.at(1)->geometry()));

        QAction* closeAllAction = findCommandAction(window.menuBar(), UiIds::Command::WindowCloseAll);
        QVERIFY(closeAllAction != nullptr);
        chooseMessageBoxButtons({QMessageBox::Discard, QMessageBox::Discard});
        closeAllAction->trigger();
        QTRY_COMPARE(mdiArea->subWindowList().size(), 0);
    }

    void reportManagerAddsDefaultsFromEmptyReportList()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        while (workspace->deck().reportCount() > 0) {
            QVERIFY(workspace->removeReportDefinition(0));
        }
        QCOMPARE(workspace->deck().reportCount(), 0);

        int dialogStep = 0;
        handleNextLegacyDialog(QStringLiteral("DESIGNREPORTS"), [&dialogStep](QDialog* dialog) {
            if (dialogStep == 0) {
                ++dialogStep;
                auto* addDefaults = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::ReportsAddDefaults));
                if (addDefaults != nullptr) {
                    addDefaults->click();
                }
                return false;
            }

            dialog->reject();
            return true;
        });

        QAction* printReportAction = findCommandAction(window.menuBar(), UiIds::Command::FilePrintReport);
        QVERIFY(printReportAction != nullptr);
        printReportAction->trigger();
        QCoreApplication::processEvents();

        QTRY_VERIFY(workspace->deck().reportCount() >= 2);
        const QStringList reportNames = [&workspace]() {
            QStringList names;
            for (const ReportDefinition& report : workspace->deck().reports()) {
                names.append(report.name);
            }
            return names;
        }();
        QVERIFY(reportNames.contains(QStringLiteral("Default Page Report")));
        QVERIFY(reportNames.contains(QStringLiteral("Default Row Report")));
    }

    void reportManagerDeletesAndRestoresReportDefinition()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        QTRY_VERIFY(workspace->deck().reportCount() >= 2);
        const int initialReportCount = workspace->deck().reportCount();
        const QString firstReportName = workspace->deck().reportAt(0).name;

        int dialogStep = 0;
        handleNextLegacyDialog(QStringLiteral("DESIGNREPORTS"), [&dialogStep](QDialog* dialog) {
            auto* reportList = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportsList));
            if (reportList != nullptr && reportList->count() > 0) {
                reportList->setCurrentRow(0);
            }

            if (dialogStep == 0) {
                ++dialogStep;
                auto* deleteButton = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::ReportsDelete));
                if (deleteButton != nullptr) {
                    deleteButton->click();
                }
                return false;
            }

            if (dialogStep == 1) {
                ++dialogStep;
                auto* undoButton = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::ReportsUndoDelete));
                if (undoButton != nullptr) {
                    undoButton->click();
                }
                return false;
            }

            dialog->reject();
            return true;
        });

        QAction* printReportAction = findCommandAction(window.menuBar(), UiIds::Command::FilePrintReport);
        QVERIFY(printReportAction != nullptr);
        printReportAction->trigger();
        QCoreApplication::processEvents();

        QTRY_COMPARE(workspace->deck().reportCount(), initialReportCount);
        QCOMPARE(workspace->deck().reportAt(0).name, firstReportName);
    }

    void printReportPreviewUsesSelectedScopeAndPreviewNavigation()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        QTRY_VERIFY(workspace->deck().reportCount() > 0);
        const QString reportName = workspace->deck().reportAt(0).name;

        bool sawPrintDialog = false;
        bool sawPreviewDialog = false;
        bool previewStateWasValid = false;
        handleNextLegacyDialog(QStringLiteral("DESIGNREPORTS"), [](QDialog* dialog) {
            auto* reportList = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportsList));
            if (reportList != nullptr && reportList->count() > 0) {
                reportList->setCurrentRow(0);
            }
            dialog->accept();
            return true;
        });
        handleNextLegacyDialog(QStringLiteral("PRINT"), [&sawPrintDialog, &sawPreviewDialog, &previewStateWasValid, reportName](QDialog* dialog) {
            sawPrintDialog = true;
            auto* allCards = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PrintAllCards));
            if (allCards != nullptr) {
                allCards->setChecked(true);
            }

            handleNextLegacyDialog(QStringLiteral("PRINTPREVIEW"), [&sawPreviewDialog, &previewStateWasValid, reportName](QDialog* previewDialog) {
                auto* title = qobject_cast<QLabel*>(
                    UiBuilder::controlById(previewDialog, UiIds::Control::PreviewTitle));
                auto* pageStatus = qobject_cast<QLabel*>(
                    UiBuilder::controlById(previewDialog, UiIds::Control::PreviewPageStatus));
                auto* nextPage = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(previewDialog, UiIds::Control::PreviewNextPage));
                auto* firstPage = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(previewDialog, UiIds::Control::PreviewFirstPage));
                sawPreviewDialog = true;
                previewStateWasValid = title != nullptr
                    && title->text() == reportName
                    && pageStatus != nullptr
                    && pageStatus->text().startsWith(QStringLiteral("Page 1 of "))
                    && nextPage != nullptr
                    && firstPage != nullptr
                    && !firstPage->isEnabled();
                previewDialog->reject();
                return true;
            });

            auto* preview = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PrintPreview));
            if (preview != nullptr) {
                preview->click();
            }
            dialog->reject();
            return true;
        });

        QAction* printReportAction = findCommandAction(window.menuBar(), UiIds::Command::FilePrintReport);
        QVERIFY(printReportAction != nullptr);
        printReportAction->trigger();
        QCoreApplication::processEvents();

        QTRY_VERIFY(sawPrintDialog);
        QTRY_VERIFY(sawPreviewDialog);
        QVERIFY(previewStateWasValid);
        QCOMPARE(reportName, workspace->deck().reportAt(0).name);
    }

    void reportManagerNewModifyAndDesignerToolsSaveReport()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        const int initialReportCount = workspace->deck().reportCount();

        handleNextLegacyDialog(QStringLiteral("REPORTFORM"), [](QDialog* dialog) {
            dialog->accept();
            return true;
        });
        handleNextLegacyDialog(QStringLiteral("DESIGNREPORTS"), [](QDialog* dialog) {
            auto* newReport = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportsNew));
            if (newReport != nullptr) {
                newReport->click();
            } else {
                dialog->reject();
            }
            return true;
        });
        QAction* printReportAction = findCommandAction(window.menuBar(), UiIds::Command::FilePrintReport);
        QVERIFY(printReportAction != nullptr);
        printReportAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QCOMPARE(designer->report().name, QStringLiteral("Untitled Report"));
        const int initialFrameCount = designer->report().frames.size();

        QAction* addText = findCommandAction(window.menuBar(), UiIds::Command::ToolAddText);
        QAction* addData = findCommandAction(window.menuBar(), UiIds::Command::ToolAddDataBox);
        QAction* addSystem = findCommandAction(window.menuBar(), UiIds::Command::ToolAddSystemData);
        QAction* addLine = findCommandAction(window.menuBar(), UiIds::Command::ToolAddLineOrBox);
        QAction* saveReport = findCommandAction(window.menuBar(), UiIds::Command::FileSaveReport);
        QVERIFY(addText != nullptr);
        QVERIFY(addData != nullptr);
        QVERIFY(addSystem != nullptr);
        QVERIFY(addLine != nullptr);
        QVERIFY(saveReport != nullptr);

        handleNextLegacyDialog(QStringLiteral("TEXTFRAME"), [](QDialog* dialog) {
            setLineEditText(dialog, UiIds::Control::FrameText, QStringLiteral("Beta report title"));
            dialog->accept();
            return true;
        });
        addText->trigger();
        QCoreApplication::processEvents();

        handleNextLegacyDialog(QStringLiteral("DATAFRAME"), [](QDialog* dialog) {
            auto* fieldList = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog, UiIds::Control::DataFrameFieldList));
            if (fieldList != nullptr && fieldList->count() > 0) {
                fieldList->setCurrentIndex(0);
            }
            if (auto* printEntire = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::DataFramePrintEntireContents))) {
                printEntire->setChecked(true);
            }
            dialog->accept();
            return true;
        });
        addData->trigger();
        QCoreApplication::processEvents();

        handleNextLegacyDialog(QStringLiteral("ADDSYSTEMBOX"), [](QDialog* dialog) {
            dialog->accept();
            return true;
        });
        addSystem->trigger();
        QCoreApplication::processEvents();

        handleNextLegacyDialog(QStringLiteral("LINEFRAME"), [](QDialog* dialog) {
            if (auto* box = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::LineFrameBox))) {
                box->setChecked(true);
            }
            if (auto* lineStyle = qobject_cast<QComboBox*>(
                    UiBuilder::controlById(dialog, UiIds::Control::LineFrameLineStyle))) {
                if (lineStyle->count() > 0) {
                    lineStyle->setCurrentIndex(std::min(2, lineStyle->count() - 1));
                }
            }
            if (auto* fillPattern = qobject_cast<QComboBox*>(
                    UiBuilder::controlById(dialog, UiIds::Control::LineFrameFillPattern))) {
                if (fillPattern->count() > 0) {
                    fillPattern->setCurrentIndex(std::min(3, fillPattern->count() - 1));
                }
            }
            setLineEditText(dialog, UiIds::Control::LineFrameCornerRadius, QStringLiteral("5"));
            dialog->accept();
            return true;
        });
        addLine->trigger();
        QCoreApplication::processEvents();

        QCOMPARE(designer->report().frames.size(), initialFrameCount + 4);
        const QVector<ReportFrameDefinition> customizedFrames = designer->report().frames;
        QVERIFY(std::any_of(customizedFrames.cbegin(), customizedFrames.cend(), [](const ReportFrameDefinition& frame) {
            return frame.kind == ReportFrameKind::Text && frame.text == QStringLiteral("Beta report title");
        }));
        QVERIFY(std::any_of(customizedFrames.cbegin(), customizedFrames.cend(), [](const ReportFrameDefinition& frame) {
            return frame.kind == ReportFrameKind::Data && frame.printEntireContentsFlag != 0;
        }));
        QVERIFY(std::any_of(customizedFrames.cbegin(), customizedFrames.cend(), [](const ReportFrameDefinition& frame) {
            return frame.kind == ReportFrameKind::LineOrBox
                && frame.lineStyle == 2
                && frame.fillPattern == 3
                && frame.cornerRadius == 5;
        }));
        saveReport->trigger();
        QCoreApplication::processEvents();
        QTRY_COMPARE(workspace->deck().reportCount(), initialReportCount + 1);
        QCOMPARE(workspace->deck().reportAt(initialReportCount).frames.size(), initialFrameCount + 4);

        QPointer<QMdiSubWindow> designerWindow = subWindowForWidget(window, designer);
        QVERIFY(designerWindow != nullptr);
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();

        auto* mdiArea = window.findChild<QMdiArea*>();
        QVERIFY(mdiArea != nullptr);
        if (QMdiSubWindow* workspaceWindow = subWindowForWidget(window, workspace)) {
            mdiArea->setActiveSubWindow(workspaceWindow);
        }
        QCoreApplication::processEvents();

        handleNextLegacyDialog(QStringLiteral("DESIGNREPORTS"), [initialReportCount](QDialog* dialog) {
            auto* list = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportsList));
            if (list != nullptr) {
                list->setCurrentRow(initialReportCount);
            }
            auto* modify = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::ReportsModify));
            if (modify != nullptr) {
                modify->click();
            } else {
                dialog->reject();
            }
            return true;
        });
        printReportAction->trigger();
        QCoreApplication::processEvents();

        auto* modifiedDesigner = window.findChild<ReportDesignerWidget*>();
        QTRY_VERIFY(modifiedDesigner != nullptr);
        QCOMPARE(modifiedDesigner->report().frames.size(), initialFrameCount + 4);

        QPointer<QMdiSubWindow> modifiedDesignerWindow = subWindowForWidget(window, modifiedDesigner);
        QVERIFY(modifiedDesignerWindow != nullptr);
        QVERIFY(modifiedDesignerWindow->close());
        QCoreApplication::processEvents();
    }

    void addAndRemoveSecurityRequiresCorrectPassword()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);
        acceptNextNewFileDialog(0);
        newAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QVERIFY(workspace != nullptr);
        QVERIFY(!workspace->hasSecurity());

        QAction* securityAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigureAddSecurity);
        QVERIFY(securityAction != nullptr);

        acceptNextSecurityDialog(QStringLiteral("ADDSECURITY"), QStringLiteral("secure"), true);
        acceptNextSecurityDialog(QStringLiteral("VERIFYPASSWORD"), QStringLiteral("secure"));
        securityAction->trigger();
        QCoreApplication::processEvents();

        QTRY_VERIFY(workspace->hasSecurity());
        QVERIFY(workspace->hasEncryptedSecurity());
        QVERIFY(workspace->securityPasswordMatches(QStringLiteral("SECURE")));

        int removeDialogStep = 0;
        handleNextLegacyDialog(QStringLiteral("REMOVESECURITY"), [&removeDialogStep](QDialog* dialog) {
            auto* passwordEdit = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::SecurityPassword));
            if (passwordEdit != nullptr) {
                passwordEdit->setText(removeDialogStep == 0 ? QStringLiteral("wrong") : QStringLiteral("secure"));
            }
            ++removeDialogStep;
            dialog->accept();
            return removeDialogStep >= 2;
        });
        chooseMessageBoxButtons({QMessageBox::Ok});
        securityAction->trigger();
        QCoreApplication::processEvents();

        QTRY_VERIFY(!workspace->hasSecurity());
        QVERIFY(!workspace->hasEncryptedSecurity());
        QCOMPARE(removeDialogStep, 2);
    }

    void findReplaceAndSortRunThroughDialogs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = writeWorkflowDeck(&directory);
        QVERIFY(!filePath.isEmpty());

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = openWorkflowDeck(&window, filePath);
        QTRY_VERIFY(workspace != nullptr);
        QCOMPARE(workspace->deck().cardCount(), 3);

        QAction* findAction = findCommandAction(window.menuBar(), UiIds::Command::SearchFind);
        QVERIFY(findAction != nullptr);
        handleNextLegacyDialog(QStringLiteral("SEARCH"), [](QDialog* dialog) {
            setComboText(dialog, UiIds::Control::SearchText, QStringLiteral("Beta"));
            dialog->accept();
            return true;
        });
        findAction->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(workspace->currentCardIndex(), 2);

        QAction* replaceAction = findCommandAction(window.menuBar(), UiIds::Command::SearchReplace);
        QVERIFY(replaceAction != nullptr);
        handleNextLegacyDialog(QStringLiteral("REPLACE"), [](QDialog* dialog) {
            setComboText(dialog, UiIds::Control::SearchText, QStringLiteral("Beta"));
            setComboText(dialog, UiIds::Control::SearchSecondText, QStringLiteral("Bravo"));
            dialog->accept();
            return true;
        });
        replaceAction->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(workspace->deck().cardAt(workspace->currentCardIndex()).valueAt(0), QStringLiteral("Bravo"));

        QAction* sortAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigureIndex);
        QVERIFY(sortAction != nullptr);
        handleNextLegacyDialog(QStringLiteral("SORT"), [](QDialog* dialog) {
            auto* field = qobject_cast<QComboBox*>(
                UiBuilder::controlById(dialog, UiIds::Control::SortFieldLevel1));
            if (field == nullptr) {
                dialog->reject();
                return true;
            }
            field->setCurrentIndex(1);
            dialog->accept();
            return true;
        });
        sortAction->trigger();
        QCoreApplication::processEvents();

        QCOMPARE(workspace->deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace->deck().cardAt(1).valueAt(0), QStringLiteral("Bravo"));
        QCOMPARE(workspace->deck().cardAt(2).valueAt(0), QStringLiteral("Charlie"));
    }

    void replaceAllRunsThroughReplaceDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = writeWorkflowDeck(&directory);
        QVERIFY(!filePath.isEmpty());

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = openWorkflowDeck(&window, filePath);
        QTRY_VERIFY(workspace != nullptr);

        QAction* replaceAction = findCommandAction(window.menuBar(), UiIds::Command::SearchReplace);
        QVERIFY(replaceAction != nullptr);
        handleNextLegacyDialog(QStringLiteral("REPLACE"), [](QDialog* dialog) {
            setComboText(dialog, UiIds::Control::SearchText, QStringLiteral("Red"));
            setComboText(dialog, UiIds::Control::SearchSecondText, QStringLiteral("Warm"));
            auto* replaceAll = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::SearchDirectionBeginning));
            if (replaceAll == nullptr) {
                dialog->reject();
                return true;
            }
            replaceAll->click();
            return true;
        });
        replaceAction->trigger();
        QCoreApplication::processEvents();

        QCOMPARE(workspace->deck().cardAt(0).valueAt(1), QStringLiteral("Blue"));
        QCOMPARE(workspace->deck().cardAt(1).valueAt(1), QStringLiteral("Warm"));
        QCOMPARE(workspace->deck().cardAt(2).valueAt(1), QStringLiteral("Warm"));
    }

    void designFromScratchCreatesDeckOnlyAfterDesignerClose()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextNewFileDialog(1);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QVERIFY(activeWorkspace(window) == nullptr);

        designer->addTextFrame();
        QVERIFY(designer->isDirty());
        const qsizetype expectedFrameCount = designer->layoutDefinition().frames.size();

        QPointer<QMdiSubWindow> designerWindow = subWindowForWidget(window, designer);
        QVERIFY(designerWindow != nullptr);

        chooseMessageBoxButtons({QMessageBox::Save, QMessageBox::Yes});
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();

        QTRY_VERIFY(activeWorkspace(window) != nullptr);
        QVERIFY(activeWorkspace(window)->deck().cardTemplateLayout().frames.size() >= expectedFrameCount);
    }

    void patternAfterDeckFileOpensDraftTemplateDesigner()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = writeWorkflowDeck(&directory);
        QVERIFY(!filePath.isEmpty());

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextPatternAfterDeckFlow(filePath);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QVERIFY(activeWorkspace(window) == nullptr);
        QVERIFY(designer->fieldNames().contains(QStringLiteral("Name")));
        QVERIFY(designer->fieldNames().contains(QStringLiteral("Group")));

        QPointer<QMdiSubWindow> designerWindow = subWindowForWidget(window, designer);
        QVERIFY(designerWindow != nullptr);
        chooseMessageBoxButtons({QMessageBox::No});
        QVERIFY(designerWindow->close());
        QCoreApplication::processEvents();
        QVERIFY(activeWorkspace(window) == nullptr);
    }

    void patternAfterLegacyDeckImportsTemplateDefinitionWhenConfigured()
    {
        const QString fixtureDir = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        const QString filePath = QDir(fixtureDir).filePath(QStringLiteral("plain.BTN"));
        if (fixtureDir.isEmpty() || !QFileInfo::exists(filePath)) {
            QSKIP("Set CARDSTACK_WINEVDM_GOLDEN_DIR to exercise legacy template-definition import.");
        }

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();
        QAction* newAction = findCommandAction(window.menuBar(), UiIds::Command::FileNew);
        QVERIFY(newAction != nullptr);

        acceptNextPatternAfterDeckFlow(filePath);
        newAction->trigger();
        QCoreApplication::processEvents();

        auto* designer = window.findChild<TemplateDesignerWidget*>();
        QTRY_VERIFY(designer != nullptr);
        QCOMPARE(designer->fieldDefinitions().size(), 11);
        QVERIFY(designer->fieldNames().contains(QStringLiteral("Product")));
        QVERIFY(designer->fieldNames().contains(QStringLiteral("Notes")));
        QVERIFY(!designer->layoutDefinition().frames.isEmpty());
    }

    void fileOpenImportReviewEditsAndSkipsRecords()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = directory.filePath(QStringLiteral("review.csv"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray csv("Name,Notes\r\nAlpha,First\r\nBeta,Second\r\n");
        QCOMPARE(file.write(csv), static_cast<qint64>(csv.size()));
        file.close();

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();
        QAction* openAction = findCommandAction(window.menuBar(), UiIds::Command::FileOpen);
        QVERIFY(openAction != nullptr);

        acceptNextOpenDialogWithFile(filePath);
        handleNextImportReview([](QDialog* dialog) {
            auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("importExamineTable"));
            if (table == nullptr || table->rowCount() != 2 || table->columnCount() != 3) {
                return false;
            }
            table->item(0, 1)->setText(QStringLiteral("Reviewed Alpha"));
            table->setCurrentCell(1, 1);
            auto* skip = dialog->findChild<QPushButton*>(QStringLiteral("importSkipRecordButton"));
            if (skip == nullptr) {
                return false;
            }
            skip->click();
            dialog->accept();
            return true;
        });
        openAction->trigger();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = activeWorkspace(window);
        QTRY_VERIFY(workspace != nullptr);
        QCOMPARE(workspace->deck().cardCount(), 1);
        QCOMPARE(workspace->deck().cardAt(0).valueAt(0), QStringLiteral("Reviewed Alpha"));
        QVERIFY(!workspace->deck().importExportProfiles().isEmpty());
        QVERIFY(workspace->deck().importExportProfiles().last().name.startsWith(QStringLiteral("Reviewed import:")));
    }

    void phoneDialerQuickDialFeedsDialDialog()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* phoneConfigAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigurePhoneDialer);
        QVERIFY(phoneConfigAction != nullptr);
        QVERIFY(phoneConfigAction->isVisible());
        QVERIFY(phoneConfigAction->isEnabled());

        handleNextLegacyDialog(QStringLiteral("PHNDEF"), [](QDialog* dialog) {
            handleNextLegacyDialog(QStringLiteral("QUICKDIAL"), [](QDialog* quickDialog) {
                auto* description = qobject_cast<QLineEdit*>(
                    UiBuilder::controlById(quickDialog, UiIds::Control::QuickDialDescription));
                auto* number = qobject_cast<QLineEdit*>(
                    UiBuilder::controlById(quickDialog, UiIds::Control::QuickDialNumber));
                if (description == nullptr || number == nullptr) {
                    quickDialog->reject();
                    return true;
                }
                description->setText(QStringLiteral("Support"));
                number->setText(QStringLiteral("555-1212"));
                quickDialog->accept();
                return true;
            });

            auto* add = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneQuickDialAdd));
            if (add == nullptr) {
                dialog->reject();
                return true;
            }
            add->click();
            QTimer::singleShot(40, dialog, &QDialog::accept);
            return true;
        });
        phoneConfigAction->trigger();
        QCoreApplication::processEvents();

        QAction* dialAction = findCommandAction(window.menuBar(), UiIds::Command::PhoneDial);
        QVERIFY(dialAction != nullptr);

        bool sawQuickDial = false;
        handleNextLegacyDialog(QStringLiteral("CALL"), [&sawQuickDial](QDialog* dialog) {
            auto* quickDials = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneQuickDials));
            if (quickDials == nullptr) {
                dialog->reject();
                return true;
            }
            for (int row = 0; row < quickDials->count(); ++row) {
                if (quickDials->item(row)->text().contains(QStringLiteral("Support"))
                    && quickDials->item(row)->text().contains(QStringLiteral("555-1212"))) {
                    sawQuickDial = true;
                    break;
                }
            }
            dialog->reject();
            return true;
        });
        dialAction->trigger();
        QCoreApplication::processEvents();

        QVERIFY(sawQuickDial);
    }

    void phoneDialerCurrentCardNumbersAndPrefixesReachDialDialog()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath = writePhoneWorkflowDeck(&directory);
        QVERIFY(!filePath.isEmpty());

        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        DeckWorkspace* workspace = openWorkflowDeck(&window, filePath);
        QTRY_VERIFY(workspace != nullptr);

        QAction* phoneConfigAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigurePhoneDialer);
        QVERIFY(phoneConfigAction != nullptr);
        handleNextLegacyDialog(QStringLiteral("PHNDEF"), [](QDialog* dialog) {
            auto* outside = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneOutsideLine));
            auto* longDistance = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLongDistance));
            if (outside != nullptr) {
                outside->setChecked(true);
            }
            if (longDistance != nullptr) {
                longDistance->setChecked(true);
            }
            setLineEditText(dialog, UiIds::Control::PhoneOutsideLinePrefix, QStringLiteral("8"));
            setLineEditText(dialog, UiIds::Control::PhoneLongDistancePrefix, QStringLiteral("1"));
            dialog->accept();
            return true;
        });
        phoneConfigAction->trigger();
        QCoreApplication::processEvents();

        QAction* dialAction = findCommandAction(window.menuBar(), UiIds::Command::PhoneDial);
        QVERIFY(dialAction != nullptr);

        bool sawCurrentCardNumber = false;
        bool sawPrefixes = false;
        handleNextLegacyDialog(QStringLiteral("CALL"), [&sawCurrentCardNumber, &sawPrefixes](QDialog* dialog) {
            auto* cardNumbers = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneCardNumbers));
            auto* number = qobject_cast<QLineEdit*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneNumber));
            auto* outside = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneOutsideLine));
            auto* longDistance = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLongDistance));
            sawPrefixes = outside != nullptr && outside->isChecked()
                && longDistance != nullptr && longDistance->isChecked();
            if (cardNumbers != nullptr) {
                for (int row = 0; row < cardNumbers->count(); ++row) {
                    if (cardNumbers->item(row)->text().contains(QStringLiteral("555-0100"))) {
                        cardNumbers->setCurrentRow(row);
                        sawCurrentCardNumber = true;
                        break;
                    }
                }
            }
            if (number != nullptr) {
                sawCurrentCardNumber = sawCurrentCardNumber && number->text() == QStringLiteral("555-0100");
            }
            dialog->reject();
            return true;
        });
        dialAction->trigger();
        QCoreApplication::processEvents();

        QVERIFY(sawCurrentCardNumber);
        QVERIFY(sawPrefixes);
    }

    void phoneDialerPersistsModernConfigurationOptions()
    {
        MainWindow window(nullptr, false);
        window.show();
        QCoreApplication::processEvents();

        QAction* phoneConfigAction = findCommandAction(window.menuBar(), UiIds::Command::ConfigurePhoneDialer);
        QVERIFY(phoneConfigAction != nullptr);

        handleNextLegacyDialog(QStringLiteral("PHNDEF"), [](QDialog* dialog) {
            auto* outside = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneOutsideLine));
            auto* longDistance = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLongDistance));
            auto* logCall = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLogCall));
            if (outside != nullptr) {
                outside->setChecked(true);
            }
            if (longDistance != nullptr) {
                longDistance->setChecked(true);
            }
            if (logCall != nullptr) {
                logCall->setChecked(true);
            }
            setLineEditText(dialog, UiIds::Control::PhoneOutsideLinePrefix, QStringLiteral("8"));
            setLineEditText(dialog, UiIds::Control::PhoneLongDistancePrefix, QStringLiteral("1"));
            setLineEditText(dialog, UiIds::Control::PhoneLocalAreaCode, QStringLiteral("212"));

            handleNextLegacyDialog(QStringLiteral("QUICKDIAL"), [](QDialog* quickDialog) {
                setLineEditText(quickDialog, UiIds::Control::QuickDialDescription, QStringLiteral("Office"));
                setLineEditText(quickDialog, UiIds::Control::QuickDialNumber, QStringLiteral("555-3434"));
                quickDialog->accept();
                return true;
            });
            if (auto* add = qobject_cast<QAbstractButton*>(
                    UiBuilder::controlById(dialog, UiIds::Control::PhoneQuickDialAdd))) {
                add->click();
            }
            QTimer::singleShot(60, dialog, &QDialog::accept);
            return true;
        });
        phoneConfigAction->trigger();
        QCoreApplication::processEvents();

        bool verified = false;
        handleNextLegacyDialog(QStringLiteral("PHNDEF"), [&verified](QDialog* dialog) {
            const auto* outside = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneOutsideLine));
            const auto* longDistance = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLongDistance));
            const auto* logCall = qobject_cast<QAbstractButton*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneLogCall));
            const auto* quickDials = qobject_cast<QListWidget*>(
                UiBuilder::controlById(dialog, UiIds::Control::PhoneQuickDials));
            const auto editText = [dialog](int controlId) {
                const auto* edit = qobject_cast<QLineEdit*>(UiBuilder::controlById(dialog, controlId));
                return edit == nullptr ? QString() : edit->text();
            };

            verified = outside != nullptr && outside->isChecked()
                && longDistance != nullptr && longDistance->isChecked()
                && logCall != nullptr && logCall->isChecked()
                && editText(UiIds::Control::PhoneOutsideLinePrefix) == QStringLiteral("8")
                && editText(UiIds::Control::PhoneLongDistancePrefix) == QStringLiteral("1")
                && editText(UiIds::Control::PhoneLocalAreaCode) == QStringLiteral("212");

            bool sawQuickDial = false;
            if (quickDials != nullptr) {
                for (int row = 0; row < quickDials->count(); ++row) {
                    const QString text = quickDials->item(row)->text();
                    if (text.contains(QStringLiteral("Office")) && text.contains(QStringLiteral("555-3434"))) {
                        sawQuickDial = true;
                        break;
                    }
                }
            }
            verified = verified && sawQuickDial;
            dialog->accept();
            return true;
        });
        phoneConfigAction->trigger();
        QTRY_VERIFY(verified);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runMainWindowWorkflowTests(int argc, char** argv)
{
    MainWindowWorkflowTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(MainWindowWorkflowTests)
#endif

#include "MainWindowWorkflowTests.moc"
