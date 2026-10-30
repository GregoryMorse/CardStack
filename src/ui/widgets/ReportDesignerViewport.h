#pragma once

#include "WidgetTeardown.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWidget>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#endif

namespace CardStack {

// View coordinates are logical screen pixels; document coordinates remain thousandths of an inch.
class ReportDesignerViewport : public QWidget {
public:
    static constexpr int RulerThickness = 28;
    static constexpr int PageInset = 16;
    static constexpr int TrailingInset = 20;
    static constexpr qreal LogicalPixelsPerInch = 96.0;
    static constexpr qreal MilsPerInch = 1000.0;
    static constexpr int HorizontalLineStep = 8;
    static constexpr int VerticalLineStep = 6;

    explicit ReportDesignerViewport(QWidget* parent = nullptr) : QWidget(parent)
    {
        m_horizontal = new QScrollBar(Qt::Horizontal, this);
        m_vertical = new QScrollBar(Qt::Vertical, this);
        m_horizontal->setObjectName(QStringLiteral("reportDesignerHorizontalScroll"));
        m_vertical->setObjectName(QStringLiteral("reportDesignerVerticalScroll"));
        m_horizontal->setAccessibleName(QCoreApplication::translate("ReportDesignerWidget", "Report horizontal scroll"));
        m_vertical->setAccessibleName(QCoreApplication::translate("ReportDesignerWidget", "Report vertical scroll"));
        for (QScrollBar* bar : {m_horizontal, m_vertical}) {
            bar->setFocusPolicy(Qt::NoFocus);
            connect(bar, &QScrollBar::valueChanged, this, [this] {
                publishGeometry();
                update();
            });
        }
        m_horizontal->setSingleStep(HorizontalLineStep);
        m_vertical->setSingleStep(VerticalLineStep);
#ifdef Q_OS_WIN
        m_scrollLock = (GetKeyState(VK_SCROLL) & 1) != 0;
#endif
        setProperty("scrollLockActive", m_scrollLock);
        qApp->installEventFilter(this);
    }

    ~ReportDesignerViewport() override { Detail::prepareWidgetTeardown(*this); }

    void setDocumentSize(QSize mils)
    {
        m_documentSize = mils.expandedTo(QSize(1, 1));
        updateScrollBars();
    }

    QPoint scrollOffset() const { return {m_horizontal->value(), m_vertical->value()}; }

    QRect drawableRect() const
    {
        return rect().adjusted(0, 0, m_vertical->isHidden() ? 0 : -m_vertical->width(),
                              m_horizontal->isHidden() ? 0 : -m_horizontal->height());
    }

    QRect contentViewportRect() const
    {
        return drawableRect().adjusted(RulerThickness, RulerThickness, 0, 0);
    }

    QRectF documentPageRect() const
    {
        const QPoint origin = QPoint(RulerThickness + PageInset, RulerThickness + PageInset) - scrollOffset();
        return {QPointF(origin), QSizeF(m_documentSize) * (LogicalPixelsPerInch / MilsPerInch)};
    }

    void ensureDocumentRectVisible(const QRectF& bounds)
    {
        const QRectF visible = QRectF(contentViewportRect()).adjusted(PageInset, PageInset, -PageInset, -PageInset);
        const auto delta = [](qreal first, qreal last, qreal nearEdge, qreal farEdge) {
            if (last - first > farEdge - nearEdge) {
                if (first > nearEdge) return qCeil(first - nearEdge);
                if (last < farEdge) return qFloor(last - farEdge);
                return 0;
            }
            if (first < nearEdge) return qFloor(first - nearEdge);
            if (last > farEdge) return qCeil(last - farEdge);
            return 0;
        };
        m_horizontal->setValue(m_horizontal->value() + delta(bounds.left(), bounds.right(), visible.left(), visible.right()));
        m_vertical->setValue(m_vertical->value() + delta(bounds.top(), bounds.bottom(), visible.top(), visible.bottom()));
    }

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress && (receiver == QApplication::focusWidget() || receiver == this)) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_ScrollLock && !key->isAutoRepeat()) {
                m_scrollLock = !m_scrollLock;
#ifdef Q_OS_WIN
                if (key->nativeVirtualKey() != 0) m_scrollLock = (GetKeyState(VK_SCROLL) & 1) != 0;
#endif
                setProperty("scrollLockActive", m_scrollLock);
            }
        }
        return QWidget::eventFilter(receiver, event);
    }

    bool handleViewportKey(QKeyEvent* event)
    {
        if (event->key() == Qt::Key_ScrollLock) {
            event->accept();
            return true;
        }
        if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier) return false;
#ifdef Q_OS_WIN
        if (event->nativeVirtualKey() != 0) m_scrollLock = (GetKeyState(VK_SCROLL) & 1) != 0;
#endif
        QScrollBar* bar = nullptr;
        int delta = 0;
        switch (event->key()) {
        case Qt::Key_PageUp: bar = m_vertical; delta = -bar->pageStep(); break;
        case Qt::Key_PageDown: bar = m_vertical; delta = bar->pageStep(); break;
        case Qt::Key_Home: bar = m_horizontal; delta = -bar->pageStep(); break;
        case Qt::Key_End: bar = m_horizontal; delta = bar->pageStep(); break;
        case Qt::Key_Left: if (m_scrollLock) { bar = m_horizontal; delta = -bar->singleStep(); } break;
        case Qt::Key_Right: if (m_scrollLock) { bar = m_horizontal; delta = bar->singleStep(); } break;
        case Qt::Key_Up: if (m_scrollLock) { bar = m_vertical; delta = -bar->singleStep(); } break;
        case Qt::Key_Down: if (m_scrollLock) { bar = m_vertical; delta = bar->singleStep(); } break;
        default: break;
        }
        if (!bar) return false;
        bar->setValue(bar->value() + delta);
        event->accept();
        return true;
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateScrollBars();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint pixels = event->pixelDelta();
        const QPoint angle = event->angleDelta();
        const bool horizontal = event->modifiers().testFlag(Qt::ShiftModifier)
            || (!angle.y() && angle.x()) || m_vertical->maximum() == 0;
        QScrollBar* bar = horizontal ? m_horizontal : m_vertical;
        const int pixelDelta = horizontal && pixels.x() ? pixels.x() : pixels.y();
        const int angleDelta = horizontal && angle.x() ? angle.x() : angle.y();
        constexpr int AngleUnitsPerNotch = 120;
        const int delta = pixelDelta ? pixelDelta
            : qRound(qreal(angleDelta) / AngleUnitsPerNotch * qApp->styleHints()->wheelScrollLines() * bar->singleStep());
        if (!delta || bar->maximum() == 0) { event->ignore(); return; }
        bar->setValue(bar->value() - delta);
        event->accept();
    }

private:
    void publishGeometry()
    {
        setProperty("designerPageRect", documentPageRect());
        setProperty("designerContentRect", contentViewportRect());
    }

    void updateScrollBars()
    {
        const int thickness = std::max(m_vertical->sizeHint().width(), m_horizontal->sizeHint().height());
        const QSizeF page = QSizeF(m_documentSize) * (LogicalPixelsPerInch / MilsPerInch);
        const QSize extent(qCeil(page.width()) + RulerThickness + PageInset + TrailingInset,
                           qCeil(page.height()) + RulerThickness + PageInset + TrailingInset);
        bool horizontal = extent.width() > width();
        bool vertical = extent.height() > height();
        for (int pass = 0; pass < 2; ++pass) {
            horizontal = extent.width() > width() - (vertical ? thickness : 0);
            vertical = extent.height() > height() - (horizontal ? thickness : 0);
        }
        const int bodyWidth = std::max(0, width() - (vertical ? thickness : 0));
        const int bodyHeight = std::max(0, height() - (horizontal ? thickness : 0));
        m_horizontal->setVisible(horizontal);
        m_vertical->setVisible(vertical);
        m_horizontal->setGeometry(0, bodyHeight, bodyWidth, thickness);
        m_vertical->setGeometry(bodyWidth, 0, thickness, bodyHeight);
        const auto halfPage = [](int extent, int step) {
            return std::max(step, ((extent / 2 + 1) / step) * step);
        };
        m_horizontal->setPageStep(halfPage(bodyWidth, HorizontalLineStep));
        m_vertical->setPageStep(halfPage(bodyHeight, VerticalLineStep));
        m_horizontal->setRange(0, std::max(0, extent.width() - bodyWidth));
        m_vertical->setRange(0, std::max(0, extent.height() - bodyHeight));
        publishGeometry();
        update();
    }

    QSize m_documentSize{1, 1};
    QScrollBar* m_horizontal = nullptr;
    QScrollBar* m_vertical = nullptr;
    bool m_scrollLock = false;
};

namespace ReportDesignerRulers {
struct Label {
    QString text;
    QRectF bounds;
    Qt::Alignment alignment;
};

inline QVector<Label> labels(const QRectF& page, const QSize& document, const QRect& viewport,
                             const QFontMetricsF& metrics)
{
    constexpr int MilsPerInch = 1000;
    constexpr qreal TextPadding = 2;
    constexpr qreal LabelGap = 4;
    QVector<Label> result;
    for (int axis = 0; axis < 2; ++axis) {
        const bool horizontal = axis == 0;
        const int units = horizontal ? document.width() : document.height();
        if (units <= 0) continue;
        const qreal start = horizontal ? page.left() : page.top();
        const qreal length = horizontal ? page.width() : page.height();
        const QRectF strip = horizontal
            ? QRectF(ReportDesignerViewport::RulerThickness, 0,
                     viewport.width() - ReportDesignerViewport::RulerThickness, ReportDesignerViewport::RulerThickness - 13)
            : QRectF(0, ReportDesignerViewport::RulerThickness,
                     ReportDesignerViewport::RulerThickness - 7, viewport.height() - ReportDesignerViewport::RulerThickness);
        const auto makeLabel = [&](int value, bool endpoint) {
            const QString text = QString::number(qreal(value) / MilsPerInch, 'g', 4);
            const qreal position = start + length * value / units;
            const QSizeF size(metrics.horizontalAdvance(text) + TextPadding, metrics.height());
            const QRectF bounds = horizontal
                ? QRectF(QPointF(endpoint ? position - size.width() : position + TextPadding, 1), size)
                : QRectF(QPointF(1, endpoint ? position - size.height() : position + TextPadding),
                         QSizeF(strip.width() - 1, size.height()));
            return Label{text, bounds, horizontal ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter};
        };
        const bool fractional = units % MilsPerInch != 0;
        const Label endpoint = makeLabel(units, true);
        QRectF previous;
        for (int value = 0; value <= units; value += MilsPerInch) {
            const Label label = makeLabel(value, false);
            if (!strip.adjusted(0, 0, 0, metrics.height()).contains(label.bounds)) continue;
            if (fractional && label.bounds.intersects(endpoint.bounds.adjusted(-LabelGap, -LabelGap, LabelGap, LabelGap))) continue;
            if (!previous.isNull() && previous.adjusted(-LabelGap, -LabelGap, LabelGap, LabelGap).intersects(label.bounds)) continue;
            result.append(label);
            previous = label.bounds;
        }
        if (fractional && strip.adjusted(0, 0, 0, metrics.height()).contains(endpoint.bounds)) result.append(endpoint);
    }
    return result;
}
} // namespace ReportDesignerRulers
} // namespace CardStack
