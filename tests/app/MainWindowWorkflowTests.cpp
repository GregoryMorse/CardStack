#include "../support/ModalDialogDriver.h"

#include "DeckWorkspace.h"
#include "MainWindow.h"
#include "UiBuilder.h"
#include "UiIds.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
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
