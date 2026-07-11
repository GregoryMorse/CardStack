#pragma once

#include "Deck.h"

#include <QAbstractTableModel>

#include <functional>

namespace CardStack {

class CardTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit CardTableModel(QObject* parent = nullptr);

    void setDeck(Deck* deck);
    void setValueChangeHandler(std::function<bool(int, int, const QString&)> handler);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    Deck* m_deck = nullptr;
    std::function<bool(int, int, const QString&)> m_valueChangeHandler;
};

} // namespace CardStack
