#pragma once

#include <QFrame>
#include <QColor>
#include <QFont>
#include <QString>
#include <QVector>

class QVBoxLayout;
class QMouseEvent;
class QPaintEvent;

namespace CardStack {

class CardDetailPanel : public QFrame {
    Q_OBJECT

public:
    struct CardTitleParts {
        QString left;
        QString middle;
        QString right;

        bool operator==(const CardTitleParts&) const = default;
        QString accessibleText() const;
    };

    struct StackEntry {
        int cardIndex = 0;
        CardTitleParts title;

        friend bool operator==(const StackEntry& left, const StackEntry& right)
        {
            return left.cardIndex == right.cardIndex && left.title == right.title;
        }
    };

    explicit CardDetailPanel(QWidget* parent = nullptr);

    QVBoxLayout* bodyLayout() const;
    const CardTitleParts& cardTitle() const;
    void setCardTitle(const CardTitleParts& cardTitle);
    void setStackEntries(const QVector<StackEntry>& entries, int currentCardIndex);
    void setAppearance(
        const QFont& indexFont,
        const QColor& indexForeground,
        const QColor& indexBackground,
        const QColor& cardBackground);

signals:
    void cardRequested(int cardIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* m_bodyLayout = nullptr;
    CardTitleParts m_cardTitle;
    QVector<StackEntry> m_stackEntries;
    int m_currentCardIndex = -1;
    QColor m_indexForeground = Qt::black;
    QColor m_indexBackground = QColor(192, 192, 192);
    QColor m_cardBackground = Qt::white;
};

} // namespace CardStack
