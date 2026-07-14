#pragma once

#include "DeckWorkspace.h"
#include "Deck.h"
#include "LegacyDeckReader.h"
#include "UiBuilder.h"

#include <QMainWindow>

#include <optional>
#include <QString>
#include <QVector>

class QAction;
class QCloseEvent;
class QShowEvent;
class QEvent;
class QLabel;
class QMdiArea;
class QMdiSubWindow;
class QResizeEvent;
class QToolBar;
class QWidget;

namespace CardStack {

class ReportDesignerWidget;
class TemplateDesignerWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        QWidget* parent = nullptr,
        bool openInitialSample = false,
        bool restorePreviousSession = false);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void createMenus();
    void createToolBar();
    void createIndexBar();
    void handleUiAction();
    void handleOpenCommand();
    void handleDeckCommand(int commandId);
    void handleNewDeckCommand();
    void handleFindCommand();
    void handleFindNextCommand();
    void handleReplaceCommand();
    void handleSortCommand();
    void handleRedefineCommand();
    void handleMergeCommand();
    void handleNewReportCommand();
    void handlePrintReportCommand();
    void handleReportDesignerCommand(int commandId);
    void handleTemplateDesignerCommand(int commandId);
    void handleSecurityCommand();
    void handleDeckDescriptionCommand();
    void handlePhoneDialCommand();
    void handlePhoneCallLogCommand();
    void handlePhoneDialerConfigCommand();
    void loadPhoneDialerSettings();
    void savePhoneDialerSettings() const;
    bool verifyNewSecurityPassword(const QString& password);
    QString normalizedSecurityPassword(const QDialog& dialog) const;
    void showHelpContents();
    void showAboutDialog();
    void showUiCommandStatus(int commandId);
    QString chooseOpenPath();
    QString chooseMergeSourcePath();
    QString chooseSaveDeckPath(const QString& suggestedPath);
    QString chooseExportTemplatePath(const QString& suggestedPath);
    QString chooseExportReportPath(const QString& suggestedPath);
    void rebuildMenus(int menuId);
    void refreshMenuForActiveWindow();
    void configureToolBarForMenu(int menuId);
    void rebuildDesignerPropertyToolbar();
    void updateToolbarCardPosition();
    void resizeIndexBarButtons();
    void updateIndexBarVisibility();
    void updateWindowMenuEntries();
    void configureSubWindowSystemMenu(QMdiSubWindow* subWindow);
    void tileSubWindowsVertical();
    void tileSubWindowsHorizontal();
    void arrangeMinimizedSubWindows();
    int showUiDialog(const QString& dialogName);
    bool configureReportForm(ReportDefinition* report);
    QString dialogNameForCommand(int commandId) const;
    QAction* findUiAction(int commandId) const;
    DeckWorkspace* activeDeckWorkspace() const;
    ReportDesignerWidget* activeReportDesigner() const;
    TemplateDesignerWidget* activeTemplateDesigner() const;
    QString activeDeckPath() const;
    void setActiveDeckPath(const QString& filePath);
    bool openDeckFromPath(const QString& filePath);
    bool probeLegacyDeckFromPath(const QString& filePath);
    bool loadDeckFromPath(const QString& filePath, Deck* deck, QString* errorMessage = nullptr);
    bool reviewImportedDeck(const QString& filePath, Deck* deck);
    bool importReportPackageFromPath(const QString& filePath);
    bool importLegacyReportStoreFromPath(const QString& filePath);
    LegacyDeckReader::Result readLegacyDeckFromPath(const QString& filePath);
    std::optional<QString> promptLegacyDeckPassword(const QString& filePath);
    bool saveActiveDeck();
    bool saveActiveDeckAs();
    bool exportTemplatePackageFromDesigner(TemplateDesignerWidget* designer);
    bool exportReportPackageFromDesigner(ReportDesignerWidget* designer);
    bool confirmCloseDeckWindow(QMdiSubWindow* subWindow);
    bool closeAllSubWindowsWithPrompts();
    QByteArray captureDeckWindowSession() const;
    void saveWindowSession(const QByteArray& deckWindows);
    bool restoreWindowSession();
    void updateDeckWindowTitle(QMdiSubWindow* subWindow, const DeckWorkspace* workspace) const;
    UiBuilder::DialogContext dialogContext() const;
    DeckWorkspace::SearchRequest searchRequestFromDialog(const QDialog& dialog) const;
    QVector<DeckWorkspace::SortLevel> sortLevelsFromDialog(const QDialog& dialog) const;
    void updateCommandState();
    void openSampleDeck();
    DeckWorkspace* openDeckWindow(const Deck& deck, const QString& filePath = {}, bool initialDirty = false);
    void openReportDesigner(DeckWorkspace* workspace, int reportIndex, const ReportDefinition& report);
    void openTemplateDesigner(DeckWorkspace* workspace);
    void openTemplateDesignerForNewDeck(Deck deck);

    struct QuickDial {
        QString description;
        QString phoneNumber;
    };

    QMdiArea* m_mdiArea = nullptr;
    QToolBar* m_buttonBar = nullptr;
    QToolBar* m_indexBar = nullptr;
    QLabel* m_cardPositionLabel = nullptr;
    QLabel* m_deckModeLabel = nullptr;
    QWidget* m_designerPropertyToolbar = nullptr;
    QVector<QuickDial> m_quickDials;
    QString m_phoneLongDistancePrefix = QStringLiteral("1");
    QString m_phoneOutsideLinePrefix = QStringLiteral("9");
    QString m_phoneLocalAreaCode;
    QByteArray m_pendingDeckWindowSession;
    bool m_phoneUseLongDistance = false;
    bool m_phoneGetOutsideLine = false;
    bool m_phoneLogCalls = false;
    bool m_enterWorksLikeTab = false;
    int m_currentMenuId = 0;
};

} // namespace CardStack
