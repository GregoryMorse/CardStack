#pragma once

#include "DeckMerge.h"
#include "Deck.h"

#include <QWidget>

class QAbstractItemView;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTableView;
class QVBoxLayout;

namespace CardStack {

class CardDetailPanel;
class CardTableModel;

class DeckWorkspace : public QWidget {
    Q_OBJECT

public:
    enum class SearchType {
        BeginsWith,
        Contains,
        DoesNotBeginWith,
        DoesNotContain,
        LessThan,
        LessThanOrEqual,
        GreaterThan,
        GreaterThanOrEqual
    };

    enum class SearchComparison {
        None,
        Or,
        And
    };

    enum class SearchDirection {
        BeginningToEnd,
        ForwardFromCurrent,
        BackwardFromCurrent
    };

    struct SearchClause {
        QString text;
        int fieldIndex = -1;
        SearchType type = SearchType::Contains;
        bool wholeWord = false;
        bool caseSensitive = false;
        bool soundsLike = false;
    };

    struct SearchRequest {
        SearchClause first;
        SearchClause second;
        SearchComparison comparison = SearchComparison::None;
        SearchDirection direction = SearchDirection::BeginningToEnd;
    };

    struct SortLevel {
        int fieldIndex = -1;
        bool descending = false;
    };

    enum class ViewMode {
        Card,
        Table
    };

    struct DeletedCard {
        CardRecord record;
        int index = 0;
    };

    struct UndoSnapshot {
        Deck deck;
        QVector<DeletedCard> deletedCards;
        int cardIndex = 0;
        int fieldIndex = 0;
        ViewMode viewMode = ViewMode::Card;
    };

    explicit DeckWorkspace(Deck deck, QWidget* parent = nullptr);

    const Deck& deck() const;
    int currentCardIndex() const;
    QStringList fieldNames() const;
    ViewMode viewMode() const;
    bool isDirty() const;
    void markDirty();
    void clearDirty();
    void setDeckDescription(const QString& description);
    void appendPhoneCallLogEntry(PhoneCallLogEntry entry);
    int removePhoneCallLogEntries(const QVector<int>& entryIndexes);

    void showCardView();
    void showTableView();
    void toggleView();

    void firstCard();
    void previousCard();
    void nextCard();
    void lastCard();
    bool jumpToIndexPrefix(const QString& prefix);
    void addCard();
    void deleteCurrentCard();
    void duplicateCurrentCard();

    void copy();
    void cut();
    void paste();
    void smartPaste();
    void clearCurrentValue();
    bool canUndo() const;
    bool canUndelete() const;
    bool undo();
    bool undeleteCard();

    void applyDataFont(const QFont& font);
    void applyNameFont(const QFont& font);
    void applyTextFont(const QFont& font);
    void applyIndexFont(const QFont& font);
    void applyAppearance(DeckAppearance appearance);

    void commitPendingEdits();
    bool redefineFields(
        const QVector<FieldDefinition>& fields,
        const QVector<int>& sourceFieldIndexes,
        QString* errorMessage = nullptr);
    void setCardTemplateLayout(const CardTemplateLayout& layout);
    bool saveReportDefinition(int reportIndex, const ReportDefinition& report);
    bool insertReportDefinition(int reportIndex, const ReportDefinition& report);
    bool removeReportDefinition(int reportIndex);

    bool hasSecurity() const;
    bool hasEncryptedSecurity() const;
    bool securityPasswordMatches(const QString& password) const;
    void setSecurity(const QString& password, bool encrypted);
    void clearSecurity();

    bool find(const SearchRequest& request);
    bool findNext();
    bool replaceCurrent(const SearchRequest& request, const QString& replacement);
    int replaceAll(const SearchRequest& request, const QString& replacement);
    void sortCards(const QVector<SortLevel>& levels);
    DeckMergeResult mergeFromDeck(const Deck& source, const DeckMergeOptions& options);

signals:
    void dirtyChanged(bool dirty);
    void cardPositionChanged(int currentCardIndex, int cardCount);

private:
    void applyStoredAppearance();
    QWidget* createCardPage();
    QWidget* createTablePage();
    void attachCardDetailPanelToCardPage();
    void attachCardDetailPanelToTablePage();
    void rebuildCardEditor();
    void refreshCardStack();
    QString cardTitle(int cardIndex) const;
    void refreshCardHeader();
    void refreshCardEditor();
    bool ensureEditableCard();
    void syncCardEditorToDeck();
    void syncModel();
    bool sortCardsByTitleIfNeeded();
    void setCurrentCardIndex(int index);
    void pushUndoSnapshot();
    void restoreSnapshot(const UndoSnapshot& snapshot);
    void replaceCards(const QVector<CardRecord>& cards);
    QString normalizedFieldValue(int fieldIndex, QString value) const;
    int selectedFieldIndex() const;
    QString currentValue() const;
    void setCurrentValue(const QString& value);
    bool setValueAt(int row, int fieldIndex, const QString& value, bool refreshModel);
    bool recordMatches(const CardRecord& record, const SearchRequest& request) const;
    bool clauseMatches(const CardRecord& record, const SearchClause& clause) const;
    bool valueMatches(QString value, QString pattern, const SearchClause& clause) const;
    int firstFieldToSearch(const SearchClause& clause) const;
    int lastFieldToSearch(const SearchClause& clause) const;
    bool moveToMatch(int cardIndex, int fieldIndex);
    int currentSearchStartIndex(SearchDirection direction) const;

    Deck m_deck;
    int m_currentCardIndex = -1;
    int m_currentFieldIndex = 0;
    ViewMode m_viewMode = ViewMode::Card;
    bool m_dirty = false;
    SearchRequest m_lastSearchRequest;
    bool m_hasLastSearchRequest = false;
    QVector<UndoSnapshot> m_undoStack;
    QVector<DeletedCard> m_deletedCards;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_cardPage = nullptr;
    QWidget* m_tablePage = nullptr;
    QVBoxLayout* m_cardPageLayout = nullptr;
    QSplitter* m_listDetailSplitter = nullptr;
    CardDetailPanel* m_cardDetailPanel = nullptr;
    QWidget* m_cardEditorContent = nullptr;
    QFormLayout* m_cardForm = nullptr;
    QTableView* m_tableView = nullptr;
    CardTableModel* m_tableModel = nullptr;
    QVector<QWidget*> m_valueEditors;
};

} // namespace CardStack
