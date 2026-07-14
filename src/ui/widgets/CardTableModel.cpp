#include "CardTableModel.h"

#include <QBrush>
#include <QColor>

#include <utility>

namespace CardStack {

CardTableModel::CardTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void CardTableModel::setDeck(Deck* deck)
{
    beginResetModel();
    m_deck = deck;
    endResetModel();
}

void CardTableModel::setAppearanceColors(
    const QColor& dataForeground,
    const QColor& dataBackground,
    const QColor& indexForeground,
    const QColor& indexBackground)
{
    if (m_dataForeground == dataForeground
        && m_dataBackground == dataBackground
        && m_indexForeground == indexForeground
        && m_indexBackground == indexBackground) {
        return;
    }

    m_dataForeground = dataForeground;
    m_dataBackground = dataBackground;
    m_indexForeground = indexForeground;
    m_indexBackground = indexBackground;
    if (rowCount() > 0 && columnCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
            {Qt::BackgroundRole, Qt::ForegroundRole});
    }
    if (columnCount() > 0) {
        emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    }
    if (rowCount() > 0) {
        emit headerDataChanged(Qt::Vertical, 0, rowCount() - 1);
    }
}

void CardTableModel::setValueChangeHandler(std::function<bool(int, int, const QString&)> handler)
{
    m_valueChangeHandler = std::move(handler);
}

int CardTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_deck == nullptr) {
        return 0;
    }

    return m_deck->cardCount();
}

int CardTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_deck == nullptr) {
        return 0;
    }

    return m_deck->fieldCount();
}

QVariant CardTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || m_deck == nullptr) {
        return {};
    }

    if (role == Qt::BackgroundRole) {
        return QBrush(m_dataBackground);
    }
    if (role == Qt::ForegroundRole) {
        return QBrush(m_dataForeground);
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return m_deck->cardAt(index.row()).valueAt(index.column());
    }
    return {};
}

bool CardTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || m_deck == nullptr || role != Qt::EditRole) {
        return false;
    }

    if (!m_valueChangeHandler || !m_valueChangeHandler(index.row(), index.column(), value.toString())) {
        return false;
    }

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant CardTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (m_deck == nullptr) {
        return {};
    }

    if (role == Qt::BackgroundRole) {
        return QBrush(m_indexBackground);
    }
    if (role == Qt::ForegroundRole) {
        return QBrush(m_indexForeground);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    if (orientation == Qt::Vertical) {
        return section + 1;
    }

    return m_deck->fieldAt(section).name();
}

Qt::ItemFlags CardTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

} // namespace CardStack
