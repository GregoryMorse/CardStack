#include "DeckWorkspace.h"

#include <QFont>
#include <QPalette>

#include "CardDetailPanel.h"
#include "CardTableModel.h"
#include "PhoneticSearch.h"

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace CardStack {

namespace {

constexpr int DefaultTableRowHeightPx = 24;
constexpr int FallbackIndexFieldIndex = 0;
constexpr char IndexSpaceBucketKey[] = "SPACE";

QString editorText(QWidget* editor)
{
    if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
        return lineEdit->text();
    }
    if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor)) {
        return textEdit->toPlainText();
    }
    return {};
}

void setEditorText(QWidget* editor, const QString& text)
{
    if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
        lineEdit->setText(text);
    } else if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor)) {
        textEdit->setPlainText(text);
    }
}

Deck deckWithCards(const Deck& source, const QVector<CardRecord>& cards)
{
    Deck updated(source.name());
    updated.setDescription(source.description());
    if (source.hasSecurity()) {
        updated.setSecurity(source.securityPassword(), source.hasEncryptedSecurity());
    }
    for (const FieldDefinition& field : source.fields()) {
        updated.addField(field);
    }
    updated.setSortKeys(source.sortKeys());
    updated.setImportExportProfiles(source.importExportProfiles());
    updated.setCardTemplateLayout(source.cardTemplateLayout());
    updated.setAppearance(source.appearance());
    for (const CardRecord& record : cards) {
        updated.addCard(record);
    }
    for (const ReportDefinition& report : source.reports()) {
        updated.addReport(report);
    }
    return updated;
}

bool cardRecordsEqual(const CardRecord& left, const CardRecord& right)
{
    const int fieldCount = std::max(left.fieldCount(), right.fieldCount());
    for (int fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
        if (left.valueAt(fieldIndex) != right.valueAt(fieldIndex)) {
            return false;
        }
    }
    return true;
}

bool cardVectorsEqual(const QVector<CardRecord>& left, const QVector<CardRecord>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (int index = 0; index < left.size(); ++index) {
        if (!cardRecordsEqual(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool fieldDefinitionsEqual(const QVector<FieldDefinition>& left, const QVector<FieldDefinition>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (int index = 0; index < left.size(); ++index) {
        if (left[index].name() != right[index].name()
            || left[index].type() != right[index].type()
            || left[index].maxLength() != right[index].maxLength()
            || left[index].showName() != right[index].showName()
            || left[index].isPhone() != right[index].isPhone()) {
            return false;
        }
    }
    return true;
}

QVector<DeckSortKey> sortKeysForRedefinedFields(
    const QVector<DeckSortKey>& sourceSortKeys,
    const QVector<int>& sourceFieldIndexes,
    int fieldCount)
{
    QVector<DeckSortKey> sortKeys;
    for (const DeckSortKey& key : sourceSortKeys) {
        const int mappedFieldIndex = sourceFieldIndexes.indexOf(key.fieldIndex);
        if (mappedFieldIndex >= 0 && mappedFieldIndex < fieldCount) {
            sortKeys.append({mappedFieldIndex, key.descending});
        }
    }
    return sortKeys;
}

CardRecord blankCardForDeck(const Deck& deck)
{
    CardRecord record;
    for (int field = 0; field < deck.fieldCount(); ++field) {
        record.appendValue({});
    }
    return record;
}

bool isBlankCardForDeck(const Deck& deck, const CardRecord& record)
{
    for (int field = 0; field < deck.fieldCount(); ++field) {
        if (!record.valueAt(field).trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

Deck deckWithSchema(
    const Deck& source,
    const QVector<FieldDefinition>& fields,
    const QVector<int>& sourceFieldIndexes)
{
    Deck updated(source.name());
    updated.setDescription(source.description());
    if (source.hasSecurity()) {
        updated.setSecurity(source.securityPassword(), source.hasEncryptedSecurity());
    }
    for (const FieldDefinition& field : fields) {
        updated.addField(field);
    }

    for (const CardRecord& sourceRecord : source.cards()) {
        CardRecord updatedRecord;
        for (int fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
            QString value;
            const int sourceFieldIndex = sourceFieldIndexes.value(fieldIndex, -1);
            if (sourceFieldIndex >= 0 && sourceFieldIndex < source.fieldCount()) {
                value = sourceRecord.valueAt(sourceFieldIndex);
            }
            const int maxLength = fields[fieldIndex].maxLength();
            if (maxLength > 0 && value.size() > maxLength) {
                value.truncate(maxLength);
            }
            updatedRecord.appendValue(value);
        }
        updated.addCard(updatedRecord);
    }

    updated.setSortKeys(sortKeysForRedefinedFields(source.sortKeys(), sourceFieldIndexes, fields.size()));
    updated.setImportExportProfiles(source.importExportProfiles());

    for (const ReportDefinition& report : source.reports()) {
        updated.addReport(report);
    }
    return updated;
}

struct MatchLocation {
    int cardIndex = 0;
    int fieldIndex = 0;
};

constexpr int MaxUndoSnapshots = 50;

int titleBucketRank(const QString& title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || !trimmed.at(0).isLetterOrNumber()) {
        return 0;
    }

    const QChar first = trimmed.at(0).toUpper();
    if (first >= QLatin1Char('A') && first <= QLatin1Char('Z')) {
        return 1 + first.unicode() - QLatin1Char('A').unicode();
    }
    if (first >= QLatin1Char('0') && first <= QLatin1Char('9')) {
        return 27 + first.unicode() - QLatin1Char('0').unicode();
    }
    return 0;
}

bool indexSortsBefore(const CardRecord& left, const CardRecord& right, const QVector<DeckSortKey>& sortKeys)
{
    for (const DeckSortKey& key : sortKeys) {
        const QString leftTitle = left.valueAt(key.fieldIndex).trimmed();
        const QString rightTitle = right.valueAt(key.fieldIndex).trimmed();
        int comparison = titleBucketRank(leftTitle) - titleBucketRank(rightTitle);
        if (comparison == 0) {
            comparison = QString::localeAwareCompare(leftTitle, rightTitle);
        }
        if (comparison != 0) {
            return key.descending ? comparison > 0 : comparison < 0;
        }
    }
    return false;
}

} // namespace

DeckWorkspace::DeckWorkspace(Deck deck, QWidget* parent)
    : QWidget(parent)
    , m_deck(std::move(deck))
    , m_stack(new QStackedWidget(this))
    , m_tableView(new QTableView(this))
    , m_tableModel(new CardTableModel(this))
{
    ensureEditableCard();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    m_cardPage = createCardPage();
    m_tablePage = createTablePage();

    m_tableModel->setDeck(&m_deck);
    m_tableModel->setValueChangeHandler([this](int row, int fieldIndex, const QString& value) {
        return setValueAt(row, fieldIndex, value, false);
    });
    m_tableView->setModel(m_tableModel);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->horizontalHeader()->setVisible(true);
    m_tableView->verticalHeader()->setVisible(true);
    m_tableView->setCornerButtonEnabled(true);
    for (QAbstractButton* button : m_tableView->findChildren<QAbstractButton*>()) {
        if (button->inherits("QTableCornerButton")) {
            button->setObjectName(QStringLiteral("deckTableCornerButton"));
            button->setText(QStringLiteral("#"));
            auto* cornerLayout = new QVBoxLayout(button);
            cornerLayout->setContentsMargins(0, 0, 0, 0);
            auto* cornerLabel = new QLabel(QStringLiteral("#"), button);
            cornerLabel->setAlignment(Qt::AlignCenter);
            cornerLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            cornerLayout->addWidget(cornerLabel);
            break;
        }
    }
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    connect(m_tableView->horizontalHeader(),
            &QHeaderView::sectionResized,
            this,
            [this](int logicalIndex, int, int newSize) {
                if (m_applyingColumnWidths || logicalIndex < 0) {
                    return;
                }
                m_deck.setFieldDisplayWidth(logicalIndex, newSize);
                markDirty();
            });
    m_tableView->verticalHeader()->setDefaultSectionSize(DefaultTableRowHeightPx);
    m_tableView->setObjectName(QStringLiteral("deckTableView"));
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (!current.isValid()) {
            return;
        }

        syncCardEditorToDeck();
        m_currentCardIndex = current.row();
        m_currentFieldIndex = current.column();
        refreshCardEditor();
    });

    m_stack->addWidget(m_cardPage);
    m_stack->addWidget(m_tablePage);
    applyStoredColumnWidths();
    applyStoredAppearance();
    showCardView();
    setCurrentCardIndex(0);
}

const Deck& DeckWorkspace::deck() const
{
    return m_deck;
}

int DeckWorkspace::currentCardIndex() const
{
    return m_currentCardIndex;
}

QStringList DeckWorkspace::fieldNames() const
{
    QStringList names;
    for (const FieldDefinition& field : m_deck.fields()) {
        names.append(field.name());
    }
    return names;
}

DeckWorkspace::ViewMode DeckWorkspace::viewMode() const
{
    return m_viewMode;
}

bool DeckWorkspace::isDirty() const
{
    return m_dirty;
}

void DeckWorkspace::clearDirty()
{
    if (!m_dirty) {
        return;
    }

    m_dirty = false;
    emit dirtyChanged(false);
}

void DeckWorkspace::appendPhoneCallLogEntry(PhoneCallLogEntry entry)
{
    m_deck.addPhoneCallLogEntry(std::move(entry));
    markDirty();
}

int DeckWorkspace::removePhoneCallLogEntries(const QVector<int>& entryIndexes)
{
    const int removed = m_deck.removePhoneCallLogEntries(entryIndexes);
    if (removed > 0) {
        markDirty();
    }
    return removed;
}

void DeckWorkspace::setDeckDescription(const QString& description)
{
    if (m_deck.description() == description) {
        return;
    }

    syncCardEditorToDeck();
    pushUndoSnapshot();
    m_deck.setDescription(description);
    markDirty();
}

void DeckWorkspace::showCardView()
{
    syncCardEditorToDeck();
    m_viewMode = ViewMode::Card;
    attachCardDetailPanelToCardPage();
    m_stack->setCurrentWidget(m_cardPage);
    refreshCardEditor();
}

void DeckWorkspace::showTableView()
{
    syncCardEditorToDeck();
    m_viewMode = ViewMode::Table;
    syncModel();
    m_stack->setCurrentWidget(m_tablePage);
    refreshCardEditor();
}

void DeckWorkspace::toggleView()
{
    if (m_viewMode == ViewMode::Table) {
        showCardView();
    } else {
        showTableView();
    }
}

void DeckWorkspace::firstCard()
{
    setCurrentCardIndex(0);
}

void DeckWorkspace::previousCard()
{
    setCurrentCardIndex(m_currentCardIndex - 1);
}

void DeckWorkspace::nextCard()
{
    setCurrentCardIndex(m_currentCardIndex + 1);
}

void DeckWorkspace::lastCard()
{
    setCurrentCardIndex(m_deck.cardCount() - 1);
}

bool DeckWorkspace::jumpToIndexPrefix(const QString& prefix)
{
    if (m_deck.cardCount() <= 0) {
        return false;
    }

    const QString normalizedPrefix = prefix.trimmed().toUpper();
    const QStringList bucketOrder = [] {
        QStringList values{QString::fromLatin1(IndexSpaceBucketKey)};
        for (int code = 'A'; code <= 'Z'; ++code) {
            values.append(QString(QChar(code)));
        }
        for (int code = '0'; code <= '9'; ++code) {
            values.append(QString(QChar(code)));
        }
        return values;
    }();
    const QString requestedBucket = normalizedPrefix.isEmpty()
        ? QString::fromLatin1(IndexSpaceBucketKey)
        : normalizedPrefix.left(1);
    const int requestedIndex = std::max(0, static_cast<int>(bucketOrder.indexOf(requestedBucket)));

    const auto titleBucket = [](const QString& title) {
        const QString trimmed = title.trimmed();
        if (trimmed.isEmpty() || !trimmed.at(0).isLetterOrNumber()) {
            return QString::fromLatin1(IndexSpaceBucketKey);
        }
        return QString(trimmed.at(0).toUpper());
    };

    const int indexField = primaryIndexFieldIndex();
    if (indexField < 0) {
        return false;
    }
    for (int offset = 0; offset < bucketOrder.size(); ++offset) {
        const QString bucket = bucketOrder.at((requestedIndex + offset) % bucketOrder.size());
        for (int cardIndex = 0; cardIndex < m_deck.cardCount(); ++cardIndex) {
            if (titleBucket(m_deck.cardAt(cardIndex).valueAt(indexField)) == bucket) {
                m_currentFieldIndex = indexField;
                setCurrentCardIndex(cardIndex);
                return true;
            }
        }
    }

    return false;
}

void DeckWorkspace::addCard()
{
    syncCardEditorToDeck();
    pushUndoSnapshot();

    m_deck.addCard(blankCardForDeck(m_deck));
    markDirty();
    syncModel();
    setCurrentCardIndex(m_deck.cardCount() - 1);
    showCardView();
}

void DeckWorkspace::deleteCurrentCard()
{
    syncCardEditorToDeck();
    if (m_deck.cardCount() <= 0 || m_currentCardIndex < 0 || m_currentCardIndex >= m_deck.cardCount()) {
        return;
    }

    const int deletedIndex = m_currentCardIndex;
    const CardRecord deletedRecord = m_deck.cardAt(deletedIndex);
    pushUndoSnapshot();

    QVector<CardRecord> remaining;
    remaining.reserve(std::max(0, m_deck.cardCount() - 1));
    for (int index = 0; index < m_deck.cardCount(); ++index) {
        if (index != deletedIndex) {
            remaining.append(m_deck.cardAt(index));
        }
    }

    m_deletedCards.append({deletedRecord, deletedIndex});
    if (remaining.isEmpty()) {
        remaining.append(blankCardForDeck(m_deck));
    }
    replaceCards(remaining);
    m_currentCardIndex = std::min(deletedIndex, m_deck.cardCount() - 1);
    refreshCardEditor();
    setCurrentCardIndex(m_currentCardIndex);
}

void DeckWorkspace::duplicateCurrentCard()
{
    syncCardEditorToDeck();
    if (m_currentCardIndex < 0 || m_currentCardIndex >= m_deck.cardCount()) {
        return;
    }

    pushUndoSnapshot();
    m_deck.addCard(m_deck.cardAt(m_currentCardIndex));
    markDirty();
    syncModel();
    setCurrentCardIndex(m_deck.cardCount() - 1);
    showCardView();
}

void DeckWorkspace::copy()
{
    QApplication::clipboard()->setText(currentValue());
}

void DeckWorkspace::cut()
{
    copy();
    clearCurrentValue();
}

void DeckWorkspace::paste()
{
    setCurrentValue(QApplication::clipboard()->text());
}

void DeckWorkspace::smartPaste()
{
    QString text = QApplication::clipboard()->text();
    if (!text.contains(QLatin1Char('\t')) &&
        !text.contains(QLatin1Char('\n')) &&
        !text.contains(QLatin1Char('\r'))) {
        paste();
        return;
    }

    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    if (text.isEmpty()) {
        return;
    }

    const int startRow = (m_viewMode == ViewMode::Table && m_tableView->currentIndex().isValid())
        ? m_tableView->currentIndex().row()
        : m_currentCardIndex;
    if (startRow < 0 || m_deck.fieldCount() <= 0) {
        return;
    }

    syncCardEditorToDeck();
    const int startField = std::clamp(selectedFieldIndex(), 0, m_deck.fieldCount() - 1);
    const QStringList rows = text.split(QLatin1Char('\n'));
    QVector<CardRecord> cards = m_deck.cards();
    bool changesData = false;

    for (int rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
        const int targetRow = startRow + rowOffset;
        while (targetRow >= cards.size()) {
            cards.append(blankCardForDeck(m_deck));
            changesData = true;
        }

        const QStringList values = rows.at(rowOffset).split(QLatin1Char('\t'));
        for (int valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            const int fieldIndex = startField + valueIndex;
            if (fieldIndex >= m_deck.fieldCount()) {
                break;
            }
            const QString normalized = normalizedFieldValue(fieldIndex, values.at(valueIndex));
            if (cards[targetRow].valueAt(fieldIndex) != normalized) {
                cards[targetRow].setValueAt(fieldIndex, normalized);
                changesData = true;
            }
        }
    }

    if (!changesData) {
        return;
    }

    pushUndoSnapshot();
    replaceCards(cards);
    m_currentCardIndex = std::clamp(startRow, 0, m_deck.cardCount() - 1);
    refreshCardEditor();
    syncModel();
}

void DeckWorkspace::clearCurrentValue()
{
    setCurrentValue({});
}

bool DeckWorkspace::canUndo() const
{
    return !m_undoStack.isEmpty();
}

bool DeckWorkspace::canUndelete() const
{
    return !m_deletedCards.isEmpty();
}

bool DeckWorkspace::undo()
{
    if (m_undoStack.isEmpty()) {
        return false;
    }

    const UndoSnapshot snapshot = m_undoStack.takeLast();
    restoreSnapshot(snapshot);
    markDirty();
    return true;
}

bool DeckWorkspace::undeleteCard()
{
    syncCardEditorToDeck();
    if (m_deletedCards.isEmpty()) {
        return false;
    }

    pushUndoSnapshot();
    const DeletedCard deleted = m_deletedCards.takeLast();
    QVector<CardRecord> cards = m_deck.cards();
    if (cards.size() == 1 && isBlankCardForDeck(m_deck, cards.first())) {
        cards.clear();
    }
    const int insertIndex = std::clamp(deleted.index, 0, static_cast<int>(cards.size()));
    cards.insert(insertIndex, deleted.record);
    replaceCards(cards);
    m_currentCardIndex = insertIndex;
    refreshCardEditor();
    setCurrentCardIndex(insertIndex);
    showCardView();
    return true;
}

void DeckWorkspace::applyDataFont(const QFont& font)
{
    pushUndoSnapshot();
    DeckAppearance appearance = m_deck.appearance();
    appearance.dataFont = font.toString();
    m_deck.setAppearance(std::move(appearance));
    for (QWidget* editor : m_valueEditors) {
        editor->setFont(font);
    }
    m_tableView->setFont(font);
    markDirty();
}

void DeckWorkspace::applyNameFont(const QFont& font)
{
    pushUndoSnapshot();
    DeckAppearance appearance = m_deck.appearance();
    appearance.nameFont = font.toString();
    m_deck.setAppearance(std::move(appearance));
    for (QLabel* label : m_cardEditorContent->findChildren<QLabel*>()) {
        if (label->objectName().startsWith(QStringLiteral("fieldCaption_"))
            || label->objectName().startsWith(QStringLiteral("fieldName_"))) {
            label->setFont(font);
        }
    }
    markDirty();
}

void DeckWorkspace::applyTextFont(const QFont& font)
{
    pushUndoSnapshot();
    DeckAppearance appearance = m_deck.appearance();
    appearance.textFont = font.toString();
    m_deck.setAppearance(std::move(appearance));
    for (QLabel* label : m_cardEditorContent->findChildren<QLabel*>(QStringLiteral("templateTextFrame"))) {
        label->setFont(font);
    }
    markDirty();
}

void DeckWorkspace::applyIndexFont(const QFont& font)
{
    pushUndoSnapshot();
    DeckAppearance appearance = m_deck.appearance();
    appearance.indexFont = font.toString();
    m_deck.setAppearance(std::move(appearance));
    m_tableView->horizontalHeader()->setFont(font);
    m_tableView->verticalHeader()->setFont(font);
    markDirty();
}

void DeckWorkspace::applyAppearance(DeckAppearance appearance)
{
    pushUndoSnapshot();
    m_deck.setAppearance(std::move(appearance));
    applyStoredAppearance();
    markDirty();
}

void DeckWorkspace::applyStoredAppearance()
{
    const DeckAppearance& appearance = m_deck.appearance();
    QFont storedFont;
    if (!appearance.dataFont.isEmpty() && storedFont.fromString(appearance.dataFont)) {
        for (QWidget* editor : m_valueEditors) {
            editor->setFont(storedFont);
        }
        m_tableView->setFont(storedFont);
    }
    if (!appearance.nameFont.isEmpty() && storedFont.fromString(appearance.nameFont)) {
        for (QLabel* label : m_cardEditorContent->findChildren<QLabel*>()) {
            if (label->objectName().startsWith(QStringLiteral("fieldCaption_"))
                || label->objectName().startsWith(QStringLiteral("fieldName_"))) {
                label->setFont(storedFont);
            }
        }
    }
    if (!appearance.textFont.isEmpty() && storedFont.fromString(appearance.textFont)) {
        for (QLabel* label : m_cardEditorContent->findChildren<QLabel*>(QStringLiteral("templateTextFrame"))) {
            label->setFont(storedFont);
        }
    }
    if (!appearance.indexFont.isEmpty() && storedFont.fromString(appearance.indexFont)) {
        m_tableView->horizontalHeader()->setFont(storedFont);
        m_tableView->verticalHeader()->setFont(storedFont);
    }

    const QPalette systemPalette = palette();
    const auto systemColor = [systemPalette](DeckColorRole role) {
        switch (role) {
        case DeckColorRole::IndexForeground:
            return systemPalette.color(QPalette::ButtonText);
        case DeckColorRole::DataForeground:
            return systemPalette.color(QPalette::Text);
        case DeckColorRole::NameForeground:
        case DeckColorRole::TextForeground:
            return systemPalette.color(QPalette::WindowText);
        case DeckColorRole::IndexBackground:
            return systemPalette.color(QPalette::Button);
        case DeckColorRole::DataBackground:
            return systemPalette.color(QPalette::Base);
        case DeckColorRole::CardBackground:
            return systemPalette.color(QPalette::Window);
        default:
            return systemPalette.color(QPalette::WindowText);
        }
    };
    auto roleColor = [&appearance, systemColor](DeckColorRole role, QPalette::ColorRole) {
        if (appearance.useSystemColors) {
            return systemColor(role);
        }
        const int index = static_cast<int>(role);
        if (index >= 0 && index < appearance.customColors.size()) {
            const QColor customColor(appearance.customColors.at(index));
            if (customColor.isValid()) {
                return customColor;
            }
        }
        return systemColor(role);
    };

    QPalette tablePalette = m_tableView->palette();
    tablePalette.setColor(QPalette::Text, roleColor(DeckColorRole::DataForeground, QPalette::Text));
    tablePalette.setColor(QPalette::Base, roleColor(DeckColorRole::DataBackground, QPalette::Base));
    tablePalette.setColor(QPalette::AlternateBase, roleColor(DeckColorRole::DataBackground, QPalette::AlternateBase));
    m_tableView->setPalette(tablePalette);
    QPalette headerPalette = m_tableView->horizontalHeader()->palette();
    headerPalette.setColor(QPalette::Button, roleColor(DeckColorRole::IndexBackground, QPalette::Button));
    headerPalette.setColor(QPalette::Window, roleColor(DeckColorRole::IndexBackground, QPalette::Window));
    headerPalette.setColor(QPalette::ButtonText, roleColor(DeckColorRole::IndexForeground, QPalette::ButtonText));
    headerPalette.setColor(QPalette::WindowText, roleColor(DeckColorRole::IndexForeground, QPalette::WindowText));
    m_tableView->horizontalHeader()->setPalette(headerPalette);
    m_tableView->verticalHeader()->setPalette(headerPalette);
    if (auto* corner = m_tableView->findChild<QAbstractButton*>(QStringLiteral("deckTableCornerButton"))) {
        corner->setPalette(headerPalette);
        for (QLabel* label : corner->findChildren<QLabel*>()) {
            label->setPalette(headerPalette);
        }
    }
    for (QWidget* editor : m_valueEditors) {
        QPalette editorPalette = editor->palette();
        editorPalette.setColor(QPalette::Text, roleColor(DeckColorRole::DataForeground, QPalette::Text));
        editorPalette.setColor(QPalette::Base, roleColor(DeckColorRole::DataBackground, QPalette::Base));
        editor->setPalette(editorPalette);
    }
    for (QLabel* label : m_cardEditorContent->findChildren<QLabel*>()) {
        QPalette labelPalette = label->palette();
        const bool isName = label->objectName().startsWith(QStringLiteral("fieldCaption_"))
            || label->objectName().startsWith(QStringLiteral("fieldName_"));
        labelPalette.setColor(
            QPalette::WindowText,
            roleColor(isName ? DeckColorRole::NameForeground : DeckColorRole::TextForeground, QPalette::WindowText));
        label->setPalette(labelPalette);
    }
    QPalette cardPalette = m_cardEditorContent->palette();
    cardPalette.setColor(QPalette::Window, roleColor(DeckColorRole::CardBackground, QPalette::Window));
    m_cardEditorContent->setAutoFillBackground(true);
    m_cardEditorContent->setPalette(cardPalette);
}

void DeckWorkspace::commitPendingEdits()
{
    syncCardEditorToDeck();
    syncModel();
}

bool DeckWorkspace::redefineFields(
    const QVector<FieldDefinition>& fields,
    const QVector<int>& sourceFieldIndexes,
    QString* errorMessage)
{
    if (fields.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("A deck must contain at least one data box.");
        }
        return false;
    }
    if (fields.size() > 40) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("legacy-compatible decks can contain at most 40 data boxes.");
        }
        return false;
    }
    if (fields.size() != sourceFieldIndexes.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("The redefine field map is incomplete.");
        }
        return false;
    }

    int notesCount = 0;
    for (const FieldDefinition& field : fields) {
        if (field.name().trimmed().isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("Every data box must have a name.");
            }
            return false;
        }
        if (field.isNotes()) {
            ++notesCount;
        }
    }
    if (notesCount > 1) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("A legacy-compatible deck can contain only one Notes data box.");
        }
        return false;
    }

    syncCardEditorToDeck();
    Deck updated = deckWithSchema(m_deck, fields, sourceFieldIndexes);
    if (fieldDefinitionsEqual(m_deck.fields(), updated.fields())
        && cardVectorsEqual(m_deck.cards(), updated.cards())
        && m_deck.sortKeys() == updated.sortKeys()) {
        return false;
    }

    pushUndoSnapshot();
    m_deck = std::move(updated);
    markDirty();
    m_currentFieldIndex = std::clamp(m_currentFieldIndex, 0, std::max(0, m_deck.fieldCount() - 1));
    if (m_deck.cardCount() <= 0) {
        m_currentCardIndex = 0;
    } else {
        m_currentCardIndex = std::clamp(m_currentCardIndex, 0, m_deck.cardCount() - 1);
    }
    rebuildCardEditor();
    syncModel();
    refreshCardEditor();
    if (m_viewMode == ViewMode::Card) {
        attachCardDetailPanelToCardPage();
        m_stack->setCurrentWidget(m_cardPage);
    } else {
        attachCardDetailPanelToTablePage();
        m_stack->setCurrentWidget(m_tablePage);
    }
    return true;
}

void DeckWorkspace::setCardTemplateLayout(const CardTemplateLayout& layout)
{
    if (m_deck.cardTemplateLayout() == layout) {
        return;
    }

    pushUndoSnapshot();
    m_deck.setCardTemplateLayout(layout);
    rebuildCardEditor();
    refreshCardEditor();
    markDirty();
}

bool DeckWorkspace::saveReportDefinition(int reportIndex, const ReportDefinition& report)
{
    if (report.name.trimmed().isEmpty()) {
        return false;
    }

    syncCardEditorToDeck();
    pushUndoSnapshot();
    if (reportIndex >= 0 && reportIndex < m_deck.reportCount()) {
        m_deck.setReport(reportIndex, report);
    } else {
        m_deck.addReport(report);
    }
    markDirty();
    return true;
}

bool DeckWorkspace::insertReportDefinition(int reportIndex, const ReportDefinition& report)
{
    if (report.name.trimmed().isEmpty()) {
        return false;
    }

    syncCardEditorToDeck();
    pushUndoSnapshot();
    m_deck.insertReport(reportIndex, report);
    markDirty();
    return true;
}

bool DeckWorkspace::removeReportDefinition(int reportIndex)
{
    if (reportIndex < 0 || reportIndex >= m_deck.reportCount()) {
        return false;
    }

    syncCardEditorToDeck();
    pushUndoSnapshot();
    m_deck.removeReport(reportIndex);
    markDirty();
    return true;
}

bool DeckWorkspace::hasSecurity() const
{
    return m_deck.hasSecurity();
}

bool DeckWorkspace::hasEncryptedSecurity() const
{
    return m_deck.hasEncryptedSecurity();
}

bool DeckWorkspace::securityPasswordMatches(const QString& password) const
{
    return hasSecurity() && m_deck.securityPassword() == password.left(8).toUpper();
}

void DeckWorkspace::setSecurity(const QString& password, bool encrypted)
{
    const QString normalizedPassword = password.left(8).toUpper();
    if (m_deck.securityPassword() == normalizedPassword && m_deck.hasEncryptedSecurity() == encrypted) {
        return;
    }

    m_deck.setSecurity(normalizedPassword, encrypted);
    markDirty();
}

void DeckWorkspace::clearSecurity()
{
    if (!m_deck.hasSecurity() && !m_deck.hasEncryptedSecurity()) {
        return;
    }

    m_deck.clearSecurity();
    markDirty();
}

QWidget* DeckWorkspace::createCardPage()
{
    auto* page = new QWidget(this);
    m_cardPageLayout = new QVBoxLayout(page);
    m_cardPageLayout->setContentsMargins(10, 8, 10, 10);
    m_cardPageLayout->setSpacing(0);

    m_cardDetailPanel = new CardDetailPanel(page);
    connect(m_cardDetailPanel, &CardDetailPanel::cardRequested, this, [this](int cardIndex) {
        setCurrentCardIndex(cardIndex);
        showCardView();
    });

    auto* scrollArea = new QScrollArea(m_cardDetailPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setObjectName(QStringLiteral("cardEditorScrollArea"));

    m_cardEditorContent = new QWidget(scrollArea);

    scrollArea->setWidget(m_cardEditorContent);
    m_cardDetailPanel->bodyLayout()->addWidget(scrollArea);
    m_cardPageLayout->addWidget(m_cardDetailPanel);

    rebuildCardEditor();
    return page;
}

QWidget* DeckWorkspace::createTablePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_listDetailSplitter = new QSplitter(Qt::Horizontal, page);
    m_listDetailSplitter->setObjectName(QStringLiteral("deckListDetailSplitter"));
    m_listDetailSplitter->addWidget(m_tableView);
    m_listDetailSplitter->setStretchFactor(0, 3);

    layout->addWidget(m_listDetailSplitter);
    return page;
}

void DeckWorkspace::attachCardDetailPanelToCardPage()
{
    if (m_cardDetailPanel == nullptr || m_cardPageLayout == nullptr) {
        return;
    }

    m_cardPageLayout->addWidget(m_cardDetailPanel);
}

void DeckWorkspace::attachCardDetailPanelToTablePage()
{
    if (m_cardDetailPanel == nullptr || m_cardPageLayout == nullptr) {
        return;
    }

    if (m_cardPageLayout->indexOf(m_cardDetailPanel) < 0) {
        m_cardPageLayout->addWidget(m_cardDetailPanel);
    }
}

void DeckWorkspace::rebuildCardEditor()
{
    if (QLayout* existingLayout = m_cardEditorContent->layout()) {
        delete existingLayout;
    }
    const QList<QWidget*> existingChildren = m_cardEditorContent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    qDeleteAll(existingChildren);

    m_cardForm = nullptr;
    m_valueEditors = QVector<QWidget*>(m_deck.fieldCount(), nullptr);

    auto makeEditor = [this](int fieldIndex, QWidget* parent) -> QWidget* {
        const FieldDefinition& field = m_deck.fieldAt(fieldIndex);
        QWidget* editor = nullptr;
        if (field.isNotes()) {
            auto* textEdit = new QPlainTextEdit(parent);
            textEdit->setMinimumHeight(90);
            textEdit->setProperty("maxLength", field.maxLength());
            QObject::connect(textEdit, &QPlainTextEdit::textChanged, textEdit, [this, textEdit, fieldIndex]() {
                const QString normalized = normalizedFieldValue(fieldIndex, textEdit->toPlainText());
                if (normalized == textEdit->toPlainText()) {
                    return;
                }

                const QSignalBlocker blocker(textEdit);
                const int cursorPosition = std::min(textEdit->textCursor().position(), static_cast<int>(normalized.size()));
                textEdit->setPlainText(normalized);
                QTextCursor cursor = textEdit->textCursor();
                cursor.setPosition(cursorPosition);
                textEdit->setTextCursor(cursor);
            });
            editor = textEdit;
        } else {
            auto* lineEdit = new QLineEdit(parent);
            if (field.maxLength() > 0) {
                lineEdit->setMaxLength(field.maxLength());
            }
            connect(lineEdit, &QLineEdit::editingFinished, this, [this]() {
                syncCardEditorToDeck();
                syncModel();
                refreshCardStack();
                refreshCardHeader();
            });
            editor = lineEdit;
        }

        editor->setProperty("fieldIndex", fieldIndex);
        editor->setObjectName(QStringLiteral("fieldValue_%1").arg(fieldIndex));
        m_valueEditors[fieldIndex] = editor;
        return editor;
    };

    const CardTemplateLayout& cardLayout = m_deck.cardTemplateLayout();
    if (!cardLayout.frames.isEmpty()) {
        constexpr int TemplateCoordinateScale = 10;
        const int canvasWidth = std::max(1, cardLayout.canvasWidth / TemplateCoordinateScale);
        const int canvasHeight = std::max(1, cardLayout.canvasHeight / TemplateCoordinateScale);
        m_cardEditorContent->setMinimumSize(canvasWidth + 24, canvasHeight + 24);
        QVector<QWidget*> templateLabels;

        auto scaledBounds = [](const QRect& bounds) {
            constexpr int Scale = 10;
            return QRect(
                bounds.left() / Scale + 12,
                bounds.top() / Scale + 12,
                std::max(1, bounds.width() / Scale),
                std::max(1, bounds.height() / Scale));
        };

        for (const CardTemplateFrame& frame : cardLayout.frames) {
            const QRect rect = scaledBounds(frame.bounds);
            if (frame.kind == CardTemplateFrameKind::Text) {
                auto* label = new QLabel(frame.text, m_cardEditorContent);
                label->setObjectName(QStringLiteral("templateTextFrame"));
                label->setGeometry(rect);
                label->setAlignment(
                    ((frame.styleFlags & CardTemplateStyleFlagAlignRight) != 0
                            ? Qt::AlignRight
                            : ((frame.styleFlags & CardTemplateStyleFlagAlignCenter) != 0 ? Qt::AlignHCenter : Qt::AlignLeft))
                        | Qt::AlignVCenter);
                label->show();
                templateLabels.append(label);
            } else if (frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox) {
                const int fieldIndex = frame.fieldIndex;
                if (fieldIndex < 0 || fieldIndex >= m_deck.fieldCount()) {
                    continue;
                }

                QWidget* editor = makeEditor(fieldIndex, m_cardEditorContent);
                const FieldDefinition& field = m_deck.fieldAt(fieldIndex);
                const QString fieldCaption = frame.text.trimmed().isEmpty()
                    ? field.name()
                    : frame.text;
                const bool hasSeparateCaption = std::any_of(
                    cardLayout.frames.cbegin(),
                    cardLayout.frames.cend(),
                    [&fieldCaption, &frame](const CardTemplateFrame& candidate) {
                        if (candidate.kind != CardTemplateFrameKind::Text) {
                            return false;
                        }
                        const bool matchingText = candidate.text.compare(fieldCaption, Qt::CaseInsensitive) == 0;
                        const bool adjacentCaption = candidate.bounds.left() < frame.bounds.right()
                            && candidate.bounds.right() > frame.bounds.left()
                            && candidate.bounds.bottom() <= frame.bounds.top()
                            && frame.bounds.top() - candidate.bounds.bottom() <= 300;
                        return matchingText || adjacentCaption;
                    });
                QRect editorRect = rect;
                if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
                    lineEdit->setPlaceholderText(field.showName() ? QString() : fieldCaption);
                    lineEdit->setToolTip(fieldCaption);
                } else if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor)) {
                    textEdit->setPlaceholderText(field.showName() ? QString() : fieldCaption);
                    textEdit->setToolTip(fieldCaption);
                }
                if (field.showName() && !hasSeparateCaption) {
                    auto* caption = new QLabel(fieldCaption, m_cardEditorContent);
                    caption->setObjectName(QStringLiteral("fieldCaption_%1").arg(fieldIndex));
                    caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    caption->setAutoFillBackground(false);
                    const int desiredWidth = caption->fontMetrics().horizontalAdvance(fieldCaption) + 8;
                    const int captionRight = rect.left() - 6;
                    const int captionLeft = std::max(0, captionRight - desiredWidth);
                    caption->setGeometry(
                        captionLeft, rect.top(), std::max(0, captionRight - captionLeft), rect.height());
                    caption->show();
                    templateLabels.append(caption);
                }
                editor->setMinimumSize(0, 0);
                editor->setMaximumSize(editorRect.size());
                editor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
                editor->setGeometry(editorRect);
                editor->show();
            } else if (frame.kind == CardTemplateFrameKind::LineOrBox) {
                auto* line = new QFrame(m_cardEditorContent);
                line->setObjectName(QStringLiteral("templateLineBoxFrame"));
                if (frame.lineBoxShape == CardTemplateLineBoxShape::HorizontalLine) {
                    line->setFrameShape(QFrame::HLine);
                } else if (frame.lineBoxShape == CardTemplateLineBoxShape::VerticalLine) {
                    line->setFrameShape(QFrame::VLine);
                } else {
                    line->setFrameShape(QFrame::Box);
                }
                line->setGeometry(rect);
                line->show();
            }
        }

        int fallbackTop = canvasHeight + 28;
        for (int fieldIndex = 0; fieldIndex < m_valueEditors.size(); ++fieldIndex) {
            if (m_valueEditors[fieldIndex] != nullptr) {
                continue;
            }
            const FieldDefinition& field = m_deck.fieldAt(fieldIndex);
            auto* label = new QLabel(field.name(), m_cardEditorContent);
            label->setObjectName(QStringLiteral("fieldName_%1").arg(fieldIndex));
            label->setGeometry(12, fallbackTop + 4, 140, 22);
            label->show();
            templateLabels.append(label);

            QWidget* editor = makeEditor(fieldIndex, m_cardEditorContent);
            editor->setGeometry(160, fallbackTop, 360, field.isNotes() ? 90 : 26);
            editor->show();
            fallbackTop += field.isNotes() ? 104 : 34;
        }
        m_cardEditorContent->setMinimumHeight(std::max(m_cardEditorContent->minimumHeight(), fallbackTop + 12));
        for (QWidget* label : templateLabels) {
            label->raise();
        }
        applyStoredAppearance();
        return;
    }

    auto* contentLayout = new QVBoxLayout(m_cardEditorContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    m_cardForm = new QFormLayout;
    m_cardForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    contentLayout->addLayout(m_cardForm);
    contentLayout->addStretch(1);

    for (int fieldIndex = 0; fieldIndex < m_deck.fieldCount(); ++fieldIndex) {
        const FieldDefinition& field = m_deck.fieldAt(fieldIndex);
        auto* label = new QLabel(field.name(), m_cardEditorContent);
        label->setObjectName(QStringLiteral("fieldName_%1").arg(fieldIndex));

        QWidget* editor = makeEditor(fieldIndex, m_cardEditorContent);
        m_cardForm->addRow(label, editor);
    }
    applyStoredAppearance();
}

void DeckWorkspace::refreshCardStack()
{
    if (m_cardDetailPanel == nullptr) {
        return;
    }

    if (m_deck.cardCount() <= 0 || m_currentCardIndex < 0) {
        m_cardDetailPanel->setStackEntries({}, -1);
        return;
    }

    constexpr int VisibleStackCards = 8;
    const int visibleCount = std::min(VisibleStackCards, m_deck.cardCount());
    QVector<CardDetailPanel::StackEntry> entries;
    entries.reserve(visibleCount);

    for (int order = 0; order < visibleCount; ++order) {
        const int cardIndex = (m_currentCardIndex + order) % m_deck.cardCount();
        entries.append({cardIndex, cardTitle(cardIndex)});
    }
    m_cardDetailPanel->setStackEntries(entries, m_currentCardIndex);
}

CardDetailPanel::CardTitleParts DeckWorkspace::cardTitle(int cardIndex) const
{
    if (cardIndex < 0 || cardIndex >= m_deck.cardCount()) {
        return {};
    }

    CardDetailPanel::CardTitleParts title;
    QString* destinations[] = {&title.left, &title.middle, &title.right};
    const QVector<DeckSortKey>& sortKeys = m_deck.sortKeys();
    if (sortKeys.isEmpty()) {
        title.left = m_deck.cardAt(cardIndex).valueAt(FallbackIndexFieldIndex).trimmed();
        return title;
    }
    for (int level = 0; level < std::min(3, static_cast<int>(sortKeys.size())); ++level) {
        const int fieldIndex = sortKeys.at(level).fieldIndex;
        if (fieldIndex >= 0 && fieldIndex < m_deck.fieldCount()) {
            *destinations[level] = m_deck.cardAt(cardIndex).valueAt(fieldIndex).trimmed();
        }
    }
    return title;
}

int DeckWorkspace::primaryIndexFieldIndex() const
{
    for (const DeckSortKey& key : m_deck.sortKeys()) {
        if (key.fieldIndex >= 0 && key.fieldIndex < m_deck.fieldCount()) {
            return key.fieldIndex;
        }
    }
    return m_deck.fieldCount() > 0 ? FallbackIndexFieldIndex : -1;
}

bool DeckWorkspace::isIndexField(int fieldIndex) const
{
    if (m_deck.sortKeys().isEmpty()) {
        return fieldIndex == FallbackIndexFieldIndex;
    }
    return std::any_of(m_deck.sortKeys().cbegin(), m_deck.sortKeys().cend(), [fieldIndex](const DeckSortKey& key) {
        return key.fieldIndex == fieldIndex;
    });
}

void DeckWorkspace::refreshCardHeader()
{
    if (m_cardDetailPanel == nullptr) {
        return;
    }

    m_cardDetailPanel->setCardTitle(cardTitle(m_currentCardIndex));
}

void DeckWorkspace::refreshCardEditor()
{
    if (ensureEditableCard()) {
        syncModel();
    }
    refreshCardStack();

    refreshCardHeader();

    if (m_deck.cardCount() <= 0 || m_currentCardIndex < 0 || m_currentCardIndex >= m_deck.cardCount()) {
        for (QWidget* editor : m_valueEditors) {
            editor->setEnabled(false);
            setEditorText(editor, {});
        }
        return;
    }

    const CardRecord& record = m_deck.cardAt(m_currentCardIndex);
    for (int fieldIndex = 0; fieldIndex < m_valueEditors.size(); ++fieldIndex) {
        m_valueEditors[fieldIndex]->setEnabled(true);
        setEditorText(m_valueEditors[fieldIndex], record.valueAt(fieldIndex));
    }
}

bool DeckWorkspace::ensureEditableCard()
{
    if (m_deck.cardCount() > 0 || m_deck.fieldCount() <= 0) {
        return false;
    }

    m_deck.addCard(blankCardForDeck(m_deck));
    m_currentCardIndex = 0;
    return true;
}

void DeckWorkspace::syncCardEditorToDeck()
{
    if (m_currentCardIndex < 0 || m_currentCardIndex >= m_deck.cardCount()) {
        return;
    }

    CardRecord& record = m_deck.cardAt(m_currentCardIndex);
    bool changed = false;
    bool titleChanged = false;
    for (int fieldIndex = 0; fieldIndex < m_valueEditors.size(); ++fieldIndex) {
        const QString normalized = normalizedFieldValue(fieldIndex, editorText(m_valueEditors[fieldIndex]));
        if (record.valueAt(fieldIndex) != normalized) {
            record.setValueAt(fieldIndex, normalized);
            changed = true;
            titleChanged = titleChanged || isIndexField(fieldIndex);
        }
    }
    if (changed) {
        markDirty();
        if (titleChanged && sortCardsByTitleIfNeeded()) {
            syncModel();
        }
    }
}

void DeckWorkspace::syncModel()
{
    m_tableModel->setDeck(&m_deck);
    applyStoredColumnWidths();
}

void DeckWorkspace::applyStoredColumnWidths()
{
    m_applyingColumnWidths = true;
    for (int column = 0; column < m_deck.fieldCount(); ++column) {
        const int width = m_deck.fieldAt(column).displayWidth();
        if (width > 0) {
            m_tableView->horizontalHeader()->resizeSection(column, width);
        }
    }
    m_applyingColumnWidths = false;
}

bool DeckWorkspace::sortCardsByTitleIfNeeded()
{
    if (m_deck.cardCount() < 2) {
        return false;
    }

    struct SortEntry {
        CardRecord record;
        int originalIndex = 0;
    };

    QVector<SortEntry> entries;
    entries.reserve(m_deck.cardCount());
    for (int cardIndex = 0; cardIndex < m_deck.cardCount(); ++cardIndex) {
        entries.append({m_deck.cardAt(cardIndex), cardIndex});
    }

    QVector<DeckSortKey> sortKeys = m_deck.sortKeys();
    if (sortKeys.isEmpty() && m_deck.fieldCount() > 0) {
        sortKeys.append({FallbackIndexFieldIndex, false});
    }
    std::stable_sort(entries.begin(), entries.end(), [&sortKeys](const SortEntry& left, const SortEntry& right) {
        return indexSortsBefore(left.record, right.record, sortKeys);
    });

    bool changed = false;
    int newCurrentIndex = std::clamp(m_currentCardIndex, 0, m_deck.cardCount() - 1);
    QVector<CardRecord> cards;
    cards.reserve(entries.size());
    for (int index = 0; index < entries.size(); ++index) {
        cards.append(entries.at(index).record);
        changed = changed || entries.at(index).originalIndex != index;
        if (entries.at(index).originalIndex == m_currentCardIndex) {
            newCurrentIndex = index;
        }
    }

    if (!changed) {
        return false;
    }

    replaceCards(cards);
    m_currentCardIndex = newCurrentIndex;
    refreshCardEditor();
    emit cardPositionChanged(m_currentCardIndex, m_deck.cardCount());
    return true;
}

void DeckWorkspace::pushUndoSnapshot()
{
    m_undoStack.append({m_deck, m_deletedCards, m_currentCardIndex, m_currentFieldIndex, m_viewMode});
    while (m_undoStack.size() > MaxUndoSnapshots) {
        m_undoStack.removeFirst();
    }
}

void DeckWorkspace::restoreSnapshot(const UndoSnapshot& snapshot)
{
    m_deck = snapshot.deck;
    ensureEditableCard();
    m_deletedCards = snapshot.deletedCards;
    m_currentCardIndex = snapshot.cardIndex;
    m_currentFieldIndex = snapshot.fieldIndex;
    m_viewMode = snapshot.viewMode;

    if (m_valueEditors.size() != m_deck.fieldCount()) {
        rebuildCardEditor();
    }
    syncModel();
    if (m_deck.cardCount() <= 0) {
        m_currentCardIndex = 0;
    } else {
        m_currentCardIndex = std::clamp(m_currentCardIndex, 0, m_deck.cardCount() - 1);
    }
    m_currentFieldIndex = std::clamp(m_currentFieldIndex, 0, std::max(0, m_deck.fieldCount() - 1));
    refreshCardEditor();
    if (m_viewMode == ViewMode::Card) {
        attachCardDetailPanelToCardPage();
    }
    m_stack->setCurrentWidget(m_viewMode == ViewMode::Card ? m_cardPage : m_tablePage);

    const QModelIndex tableIndex = m_tableModel->index(m_currentCardIndex, m_currentFieldIndex);
    if (tableIndex.isValid()) {
        m_tableView->setCurrentIndex(tableIndex);
        m_tableView->scrollTo(tableIndex);
    }
}

void DeckWorkspace::markDirty()
{
    if (m_dirty) {
        return;
    }

    m_dirty = true;
    emit dirtyChanged(true);
}

void DeckWorkspace::replaceCards(const QVector<CardRecord>& cards)
{
    m_deck = deckWithCards(m_deck, cards);
    markDirty();
    syncModel();
}

QString DeckWorkspace::normalizedFieldValue(int fieldIndex, QString value) const
{
    const int maxLength = m_deck.fieldAt(fieldIndex).maxLength();
    if (maxLength > 0 && value.size() > maxLength) {
        value.truncate(maxLength);
    }
    return value;
}

void DeckWorkspace::setCurrentCardIndex(int index)
{
    syncCardEditorToDeck();
    if (ensureEditableCard()) {
        syncModel();
    }
    if (m_deck.cardCount() <= 0) {
        m_currentCardIndex = 0;
        refreshCardEditor();
        emit cardPositionChanged(m_currentCardIndex, m_deck.cardCount());
        return;
    }

    m_currentCardIndex = std::clamp(index, 0, m_deck.cardCount() - 1);
    refreshCardEditor();
    emit cardPositionChanged(m_currentCardIndex, m_deck.cardCount());

    m_currentFieldIndex = std::clamp(m_currentFieldIndex, 0, std::max(0, m_deck.fieldCount() - 1));
    const QModelIndex tableIndex = m_tableModel->index(m_currentCardIndex, m_currentFieldIndex);
    if (tableIndex.isValid()) {
        m_tableView->setCurrentIndex(tableIndex);
        m_tableView->scrollTo(tableIndex);
    }
}

int DeckWorkspace::selectedFieldIndex() const
{
    if (m_viewMode == ViewMode::Table && m_tableView->currentIndex().isValid()) {
        return m_tableView->currentIndex().column();
    }

    QWidget* focused = QApplication::focusWidget();
    while (focused != nullptr && focused != this) {
        const QVariant fieldIndex = focused->property("fieldIndex");
        if (fieldIndex.isValid()) {
            return fieldIndex.toInt();
        }
        focused = focused->parentWidget();
    }

    return 0;
}

QString DeckWorkspace::currentValue() const
{
    const int row = (m_viewMode == ViewMode::Table && m_tableView->currentIndex().isValid())
        ? m_tableView->currentIndex().row()
        : m_currentCardIndex;
    return m_deck.cardAt(row).valueAt(selectedFieldIndex());
}

void DeckWorkspace::setCurrentValue(const QString& value)
{
    const int row = (m_viewMode == ViewMode::Table && m_tableView->currentIndex().isValid())
        ? m_tableView->currentIndex().row()
        : m_currentCardIndex;
    setValueAt(row, selectedFieldIndex(), value, true);
}

bool DeckWorkspace::setValueAt(int row, int fieldIndex, const QString& value, bool refreshModel)
{
    if (row < 0 || row >= m_deck.cardCount() || fieldIndex < 0 || fieldIndex >= m_deck.fieldCount()) {
        return false;
    }

    const QString normalized = normalizedFieldValue(fieldIndex, value);
    if (m_deck.cardAt(row).valueAt(fieldIndex) == normalized) {
        return false;
    }

    pushUndoSnapshot();
    m_deck.cardAt(row).setValueAt(fieldIndex, normalized);
    markDirty();
    if (isIndexField(fieldIndex)) {
        m_currentCardIndex = row;
        if (sortCardsByTitleIfNeeded()) {
            syncModel();
            return true;
        }
    }
    if (row == m_currentCardIndex) {
        refreshCardEditor();
    }
    if (refreshModel) {
        syncModel();
    }
    return true;
}

bool DeckWorkspace::find(const SearchRequest& request)
{
    syncCardEditorToDeck();
    m_lastSearchRequest = request;
    m_hasLastSearchRequest = true;

    const auto matchingField = [this](const CardRecord& record, const SearchClause& clause) {
        const int firstField = firstFieldToSearch(clause);
        const int lastField = lastFieldToSearch(clause);
        for (int fieldIndex = firstField; fieldIndex <= lastField; ++fieldIndex) {
            if (valueMatches(record.valueAt(fieldIndex), clause.text, clause)) {
                return fieldIndex;
            }
        }
        return -1;
    };

    const int step = request.direction == SearchDirection::BackwardFromCurrent ? -1 : 1;
    int cardIndex = currentSearchStartIndex(request.direction);
    while (cardIndex >= 0 && cardIndex < m_deck.cardCount()) {
        const CardRecord& record = m_deck.cardAt(cardIndex);
        const int firstMatch = matchingField(record, request.first);
        const int secondMatch = request.comparison == SearchComparison::None
            ? -1
            : matchingField(record, request.second);
        const bool matches = request.comparison == SearchComparison::None
            ? firstMatch >= 0
            : request.comparison == SearchComparison::And
                ? firstMatch >= 0 && secondMatch >= 0
                : firstMatch >= 0 || secondMatch >= 0;
        if (matches) {
            return moveToMatch(cardIndex, firstMatch >= 0 ? firstMatch : secondMatch);
        }
        cardIndex += step;
    }

    return false;
}

bool DeckWorkspace::findNext()
{
    if (!m_hasLastSearchRequest) {
        return false;
    }

    SearchRequest request = m_lastSearchRequest;
    if (request.direction == SearchDirection::BeginningToEnd) {
        request.direction = SearchDirection::ForwardFromCurrent;
    }

    const int step = request.direction == SearchDirection::BackwardFromCurrent ? -1 : 1;
    const int originalIndex = m_currentCardIndex;
    m_currentCardIndex = std::clamp(m_currentCardIndex + step, 0, std::max(0, m_deck.cardCount() - 1));
    const bool found = find(request);
    if (!found) {
        setCurrentCardIndex(originalIndex);
    }
    return found;
}

bool DeckWorkspace::replaceCurrent(const SearchRequest& request, const QString& replacement)
{
    syncCardEditorToDeck();
    if (m_currentCardIndex < 0 || m_currentCardIndex >= m_deck.cardCount()) {
        return false;
    }

    CardRecord& record = m_deck.cardAt(m_currentCardIndex);
    SearchClause clause = request.first;
    clause.fieldIndex = m_currentFieldIndex;
    if (!clauseMatches(record, clause)) {
        if (!find(request)) {
            return false;
        }
        return replaceCurrent(request, replacement);
    }

    const QString normalizedReplacement = normalizedFieldValue(m_currentFieldIndex, replacement);
    if (record.valueAt(m_currentFieldIndex) == normalizedReplacement) {
        return true;
    }

    pushUndoSnapshot();
    record.setValueAt(m_currentFieldIndex, normalizedReplacement);
    markDirty();
    refreshCardEditor();
    syncModel();
    return true;
}

int DeckWorkspace::replaceAll(const SearchRequest& request, const QString& replacement)
{
    syncCardEditorToDeck();
    QVector<MatchLocation> matches;
    bool changesData = false;
    for (int cardIndex = 0; cardIndex < m_deck.cardCount(); ++cardIndex) {
        const CardRecord& record = m_deck.cardAt(cardIndex);
        const int firstField = firstFieldToSearch(request.first);
        const int lastField = lastFieldToSearch(request.first);
        for (int fieldIndex = firstField; fieldIndex <= lastField; ++fieldIndex) {
            SearchRequest fieldRequest = request;
            if (fieldRequest.first.fieldIndex < 0) {
                fieldRequest.first.fieldIndex = fieldIndex;
            }
            if (recordMatches(record, fieldRequest)) {
                matches.append({cardIndex, fieldIndex});
                changesData = changesData || record.valueAt(fieldIndex) != normalizedFieldValue(fieldIndex, replacement);
            }
        }
    }

    if (changesData) {
        pushUndoSnapshot();
        for (const MatchLocation& match : matches) {
            m_deck.cardAt(match.cardIndex).setValueAt(
                match.fieldIndex,
                normalizedFieldValue(match.fieldIndex, replacement));
        }
        markDirty();
    }

    syncModel();
    refreshCardEditor();
    return matches.size();
}

void DeckWorkspace::sortCards(const QVector<SortLevel>& levels)
{
    syncCardEditorToDeck();

    QVector<DeckSortKey> sortKeys;
    for (const SortLevel& level : levels) {
        if (level.fieldIndex >= 0 && level.fieldIndex < m_deck.fieldCount()) {
            sortKeys.append({level.fieldIndex, level.descending});
        }
    }

    QVector<CardRecord> cards = m_deck.cards();
    std::stable_sort(cards.begin(), cards.end(), [levels](const CardRecord& left, const CardRecord& right) {
        for (const SortLevel& level : levels) {
            if (level.fieldIndex < 0) {
                continue;
            }

            const int comparison = QString::localeAwareCompare(
                left.valueAt(level.fieldIndex),
                right.valueAt(level.fieldIndex));
            if (comparison == 0) {
                continue;
            }

            return level.descending ? comparison > 0 : comparison < 0;
        }

        return false;
    });

    if (cardVectorsEqual(cards, m_deck.cards()) && m_deck.sortKeys() == sortKeys) {
        return;
    }

    pushUndoSnapshot();
    replaceCards(cards);
    m_deck.setSortKeys(std::move(sortKeys));
    markDirty();
    m_currentCardIndex = 0;
    refreshCardEditor();
    setCurrentCardIndex(0);
}

DeckMergeResult DeckWorkspace::mergeFromDeck(const Deck& source, const DeckMergeOptions& options)
{
    syncCardEditorToDeck();

    Deck mergedDeck = m_deck;
    DeckMergeResult result = mergeDecks(&mergedDeck, source, options);
    if (!result.ok() || result.cardsMerged == 0) {
        return result;
    }

    pushUndoSnapshot();
    m_deck = std::move(mergedDeck);
    markDirty();
    syncModel();
    setCurrentCardIndex(m_deck.cardCount() - result.cardsMerged);
    showTableView();
    return result;
}

bool DeckWorkspace::recordMatches(const CardRecord& record, const SearchRequest& request) const
{
    const bool firstMatches = clauseMatches(record, request.first);
    if (request.comparison == SearchComparison::None) {
        return firstMatches;
    }

    const bool secondMatches = clauseMatches(record, request.second);
    if (request.comparison == SearchComparison::And) {
        return firstMatches && secondMatches;
    }

    return firstMatches || secondMatches;
}

bool DeckWorkspace::clauseMatches(const CardRecord& record, const SearchClause& clause) const
{
    if (clause.text.isEmpty()) {
        return true;
    }

    const int firstField = firstFieldToSearch(clause);
    const int lastField = lastFieldToSearch(clause);
    for (int fieldIndex = firstField; fieldIndex <= lastField; ++fieldIndex) {
        if (valueMatches(record.valueAt(fieldIndex), clause.text, clause)) {
            return true;
        }
    }

    return false;
}

bool DeckWorkspace::valueMatches(QString value, QString pattern, const SearchClause& clause) const
{
    if (!clause.caseSensitive) {
        value = value.toLower();
        pattern = pattern.toLower();
    }

    if (clause.soundsLike) {
        value = soundsLikeKey(value);
        pattern = soundsLikeKey(pattern);
    }

    const Qt::CaseSensitivity caseSensitivity = clause.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    auto containsWholeWord = [&]() {
        const QRegularExpression::PatternOptions options =
            clause.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
        const QRegularExpression expression(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(pattern)),
            options);
        return expression.match(value).hasMatch();
    };

    switch (clause.type) {
    case SearchType::BeginsWith:
        if (clause.wholeWord) {
            const int firstSpace = value.indexOf(QRegularExpression(QStringLiteral("\\s")));
            const QString firstWord = firstSpace < 0 ? value : value.left(firstSpace);
            return firstWord.compare(pattern, caseSensitivity) == 0;
        }
        return value.startsWith(pattern, caseSensitivity);
    case SearchType::Contains:
        return clause.wholeWord ? containsWholeWord() : value.contains(pattern, caseSensitivity);
    case SearchType::DoesNotBeginWith:
        return !value.startsWith(pattern, caseSensitivity);
    case SearchType::DoesNotContain:
        return clause.wholeWord ? !containsWholeWord() : !value.contains(pattern, caseSensitivity);
    case SearchType::LessThan:
        return QString::compare(value, pattern, caseSensitivity) < 0;
    case SearchType::LessThanOrEqual:
        return QString::compare(value, pattern, caseSensitivity) <= 0;
    case SearchType::GreaterThan:
        return QString::compare(value, pattern, caseSensitivity) > 0;
    case SearchType::GreaterThanOrEqual:
        return QString::compare(value, pattern, caseSensitivity) >= 0;
    }

    return false;
}

int DeckWorkspace::firstFieldToSearch(const SearchClause& clause) const
{
    if (clause.fieldIndex >= 0) {
        return std::clamp(clause.fieldIndex, 0, std::max(0, m_deck.fieldCount() - 1));
    }

    return 0;
}

int DeckWorkspace::lastFieldToSearch(const SearchClause& clause) const
{
    if (clause.fieldIndex >= 0) {
        return firstFieldToSearch(clause);
    }

    return std::max(0, m_deck.fieldCount() - 1);
}

bool DeckWorkspace::moveToMatch(int cardIndex, int fieldIndex)
{
    m_currentFieldIndex = std::clamp(fieldIndex, 0, std::max(0, m_deck.fieldCount() - 1));
    setCurrentCardIndex(cardIndex);

    const QModelIndex tableIndex = m_tableModel->index(m_currentCardIndex, m_currentFieldIndex);
    if (tableIndex.isValid()) {
        m_tableView->setCurrentIndex(tableIndex);
        m_tableView->scrollTo(tableIndex);
    }

    if (m_currentFieldIndex >= 0 && m_currentFieldIndex < m_valueEditors.size()) {
        m_valueEditors[m_currentFieldIndex]->setFocus();
    }
    return true;
}

int DeckWorkspace::currentSearchStartIndex(SearchDirection direction) const
{
    if (m_deck.cardCount() <= 0) {
        return 0;
    }

    switch (direction) {
    case SearchDirection::BeginningToEnd:
        return 0;
    case SearchDirection::ForwardFromCurrent:
    case SearchDirection::BackwardFromCurrent:
        return std::clamp(m_currentCardIndex, 0, m_deck.cardCount() - 1);
    }

    return 0;
}

} // namespace CardStack
