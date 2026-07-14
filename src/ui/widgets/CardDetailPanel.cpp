#include "CardDetailPanel.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace CardStack {
namespace {

constexpr int MaxVisibleStackCards = 8;
constexpr int StackOffsetXPx = 40;
constexpr int StackOffsetYPx = 30;
constexpr int CardMarginPx = 8;
constexpr int CardCornerRadiusPx = 12;
constexpr int HeaderHeightPx = 28;

QRectF activeCardRectFor(const QRect& widgetRect, int visibleCount)
{
    const int stackCount = std::max(1, visibleCount);
    const int maxOffsetX = (stackCount - 1) * StackOffsetXPx;
    const int maxOffsetY = (stackCount - 1) * StackOffsetYPx;
    return QRectF(
        CardMarginPx,
        CardMarginPx + maxOffsetY,
        std::max(1, widgetRect.width() - 2 * CardMarginPx - maxOffsetX),
        std::max(1, widgetRect.height() - 2 * CardMarginPx - maxOffsetY));
}

QRectF cardRectForOrder(const QRect& widgetRect, int visibleCount, int order)
{
    return activeCardRectFor(widgetRect, visibleCount).translated(order * StackOffsetXPx, -order * StackOffsetYPx);
}

void drawCardFrame(
    QPainter* painter,
    const QRectF& cardRect,
    const CardDetailPanel::CardTitleParts& title,
    const QFont& baseFont,
    bool active)
{
    const QRectF shadowRect = cardRect.translated(0, active ? 3 : 2);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, active ? 28 : 18));
    painter->drawRoundedRect(shadowRect, CardCornerRadiusPx, CardCornerRadiusPx);

    QLinearGradient bodyGradient(cardRect.topLeft(), cardRect.bottomRight());
    bodyGradient.setColorAt(0.0, active ? QColor(255, 253, 246) : QColor(239, 245, 246));
    bodyGradient.setColorAt(1.0, active ? QColor(244, 238, 224) : QColor(215, 229, 232));
    painter->setBrush(bodyGradient);
    painter->setPen(QPen(active ? QColor(181, 164, 132) : QColor(138, 160, 165), 1));
    painter->drawRoundedRect(cardRect, CardCornerRadiusPx, CardCornerRadiusPx);

    const QRectF headerRect(cardRect.left(), cardRect.top(), cardRect.width(), HeaderHeightPx);
    QLinearGradient headerGradient(headerRect.topLeft(), headerRect.bottomLeft());
    headerGradient.setColorAt(0.0, active ? QColor(44, 74, 78) : QColor(219, 231, 233));
    headerGradient.setColorAt(1.0, active ? QColor(34, 55, 59) : QColor(196, 215, 219));
    painter->setBrush(headerGradient);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(headerRect, CardCornerRadiusPx, CardCornerRadiusPx);
    painter->fillRect(
        QRectF(headerRect.left(), headerRect.bottom() - CardCornerRadiusPx, headerRect.width(), CardCornerRadiusPx),
        active ? QColor(34, 55, 59) : QColor(196, 215, 219));

    painter->setPen(active ? QColor(255, 250, 235) : QColor(35, 56, 60));
    QFont titleFont = baseFont;
    titleFont.setBold(active);
    titleFont.setPointSize(std::max(9, titleFont.pointSize()));
    painter->setFont(titleFont);
    const QRectF titleArea = headerRect.adjusted(14, 2, -14, -2);
    const qreal sectionWidth = titleArea.width() / 3.0;
    const QFontMetrics metrics(titleFont);
    const struct {
        QString text;
        Qt::Alignment alignment;
    } sections[] = {
        {title.left, Qt::AlignLeft | Qt::AlignVCenter},
        {title.middle, Qt::AlignHCenter | Qt::AlignVCenter},
        {title.right, Qt::AlignRight | Qt::AlignVCenter},
    };
    for (int index = 0; index < 3; ++index) {
        const QRectF section(
            titleArea.left() + index * sectionWidth,
            titleArea.top(),
            sectionWidth,
            titleArea.height());
        painter->save();
        painter->setClipRect(section);
        painter->drawText(
            section.adjusted(3, 0, -3, 0),
            sections[index].alignment,
            metrics.elidedText(
                sections[index].text,
                Qt::ElideRight,
                std::max(0, static_cast<int>(section.width()) - 6)));
        painter->restore();
    }

    if (active) {
        painter->setPen(QPen(QColor(214, 200, 169), 1));
        painter->drawLine(
            QPointF(cardRect.left() + 18, headerRect.bottom() + 8),
            QPointF(cardRect.right() - 18, headerRect.bottom() + 8));
    }
}

} // namespace

CardDetailPanel::CardDetailPanel(QWidget* parent)
    : QFrame(parent)
    , m_bodyLayout(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("cardDetailPanel"));
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, false);
    setMinimumWidth(320);

    m_bodyLayout->setContentsMargins(22, CardMarginPx + HeaderHeightPx + 14, 22, 22);
    m_bodyLayout->setSpacing(12);
}

QVBoxLayout* CardDetailPanel::bodyLayout() const
{
    return m_bodyLayout;
}

QString CardDetailPanel::CardTitleParts::accessibleText() const
{
    QStringList values;
    for (const QString& value : {left, middle, right}) {
        if (!value.trimmed().isEmpty()) {
            values.append(value.trimmed());
        }
    }
    return values.join(QStringLiteral(" | "));
}

const CardDetailPanel::CardTitleParts& CardDetailPanel::cardTitle() const
{
    return m_cardTitle;
}

void CardDetailPanel::setCardTitle(const CardTitleParts& cardTitle)
{
    if (m_cardTitle == cardTitle) {
        return;
    }

    m_cardTitle = cardTitle;
    setAccessibleName(cardTitle.accessibleText());
    update();
}

void CardDetailPanel::setStackEntries(const QVector<StackEntry>& entries, int currentCardIndex)
{
    if (m_stackEntries == entries && m_currentCardIndex == currentCardIndex) {
        return;
    }

    m_stackEntries = entries;
    m_currentCardIndex = currentCardIndex;
    const int visibleCount = std::max(1, std::min(MaxVisibleStackCards, static_cast<int>(m_stackEntries.size())));
    m_bodyLayout->setContentsMargins(
        22,
        CardMarginPx + (visibleCount - 1) * StackOffsetYPx + HeaderHeightPx + 14,
        22 + (visibleCount - 1) * StackOffsetXPx,
        22);
    update();
}

void CardDetailPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QVector<StackEntry> entries = m_stackEntries;
        if (entries.isEmpty()) {
            entries.append({m_currentCardIndex, m_cardTitle});
        }
        const int visibleCount = std::min(MaxVisibleStackCards, static_cast<int>(entries.size()));
        for (int order = visibleCount - 1; order >= 1; --order) {
            const StackEntry& entry = entries.at(order);
            const QRectF cardRect = cardRectForOrder(rect(), visibleCount, order);
            const QRect headerRect = QRectF(
                cardRect.left(),
                cardRect.top(),
                cardRect.width(),
                HeaderHeightPx).toAlignedRect();
            if (entry.cardIndex != m_currentCardIndex && headerRect.contains(event->pos())) {
                emit cardRequested(entry.cardIndex);
                event->accept();
                return;
            }
        }
    }

    QFrame::mousePressEvent(event);
}

void CardDetailPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QVector<StackEntry> entries = m_stackEntries;
    if (entries.isEmpty()) {
        entries.append({m_currentCardIndex, m_cardTitle});
    }
    const int visibleCount = std::min(MaxVisibleStackCards, static_cast<int>(entries.size()));
    const QFont baseFont = font();

    for (int order = visibleCount - 1; order >= 1; --order) {
        const StackEntry& entry = entries.at(order);
        const QRectF cardRect = cardRectForOrder(rect(), visibleCount, order);
        drawCardFrame(&painter, cardRect, entry.title, baseFont, false);
    }

    const QRectF activeRect = cardRectForOrder(rect(), visibleCount, 0);
    drawCardFrame(&painter, activeRect, m_cardTitle, baseFont, true);
}

} // namespace CardStack
