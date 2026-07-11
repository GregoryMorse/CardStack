#pragma once

#include <QFrame>
#include <QString>
#include <QVector>

class QVBoxLayout;
class QMouseEvent;
class QPaintEvent;

namespace CardStack {

class CardDetailPanel : public QFrame {
    Q_OBJECT

public:
    struct StackEntry {
        int cardIndex = 0;
        QString title;

        friend bool operator==(const StackEntry& left, const StackEntry& right)
        {
            return left.cardIndex == right.cardIndex && left.title == right.title;
        }
    };

    explicit CardDetailPanel(QWidget* parent = nullptr);

    QVBoxLayout* bodyLayout() const;
    void setCardTitle(const QString& cardTitle);
    void setStackEntries(const QVector<StackEntry>& entries, int currentCardIndex);

signals:
    void cardRequested(int cardIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* m_bodyLayout = nullptr;
    QString m_cardTitle;
    QVector<StackEntry> m_stackEntries;
    int m_currentCardIndex = -1;
};

} // namespace CardStack
