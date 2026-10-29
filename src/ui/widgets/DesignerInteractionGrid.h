#pragma once

#include "DesignerResizeHandles.h"
#include "DesignerSelection.h"
#include <QtMath>

namespace CardStack::DesignerInteraction {

inline constexpr int SourceGridPixels = 3;
inline constexpr qreal ReportUnitsPerSourcePixel = 1000.0 / 96.0;
inline constexpr qreal TemplateUnitsPerSourcePixel = 10.0;
inline constexpr int CopyLeftSourcePixel = 10;
inline constexpr int CopyTopInsetSourcePixels = 6;
inline constexpr int DefaultIndexHeaderSourcePixels = 22;
inline constexpr int IndexHeaderPaddingSourcePixels = 2;

struct Grid {
    qreal unitsPerPixel;

    static int snapPixel(qreal value)
    {
        const int pixel = qRound(value);
        return qRound(qreal(pixel) / SourceGridPixels) * SourceGridPixels;
    }

    // Preserve an imported off-pixel residual; convert both endpoints to avoid repeated-step drift.
    int deltaAt(int position, int pixels) const
    {
        const int origin = qRound(position / unitsPerPixel);
        return qRound((origin + pixels) * unitsPerPixel) - qRound(origin * unitsPerPixel);
    }

    QPoint modelDelta(QPoint origin, QPoint pixels) const
    {
        return {deltaAt(origin.x(), pixels.x()), deltaAt(origin.y(), pixels.y())};
    }

    QPoint pointerDelta(QPointF first, QPointF last) const
    {
        return {snapPixel(last.x() / unitsPerPixel) - snapPixel(first.x() / unitsPerPixel),
                snapPixel(last.y() / unitsPerPixel) - snapPixel(first.y() / unitsPerPixel)};
    }

    QPoint copyDelta(const QRect& bounds, int indexHeaderPixels = 0) const
    {
        const int x = snapPixel(CopyLeftSourcePixel - qRound(bounds.left() / unitsPerPixel));
        const int y = snapPixel(indexHeaderPixels + CopyTopInsetSourcePixels - qRound(bounds.top() / unitsPerPixel));
        return modelDelta(bounds.topLeft(), {x, y});
    }

    QRect resized(const QRect& start, QPoint pixels, DesignerResize::Handle handle, QSize page,
                  int minimumPixels = 6) const
    {
        const bool left = handle == DesignerResize::Handle::Left || handle == DesignerResize::Handle::TopLeft
            || handle == DesignerResize::Handle::BottomLeft;
        const bool right = handle == DesignerResize::Handle::Right || handle == DesignerResize::Handle::TopRight
            || handle == DesignerResize::Handle::BottomRight;
        const bool top = handle == DesignerResize::Handle::Top || handle == DesignerResize::Handle::TopLeft
            || handle == DesignerResize::Handle::TopRight;
        const bool bottom = handle == DesignerResize::Handle::Bottom || handle == DesignerResize::Handle::BottomLeft
            || handle == DesignerResize::Handle::BottomRight;
        const int minimumWidth = std::min(start.width(), std::max(1, qRound(minimumPixels * unitsPerPixel)));
        const int minimumHeight = std::min(start.height(), std::max(1, qRound(minimumPixels * unitsPerPixel)));
        QRect result = start;
        if (left) result.setLeft(std::clamp(start.left() + deltaAt(start.left(), pixels.x()),
                                            0, std::max(0, start.left() + start.width() - minimumWidth)));
        if (right) result.setRight(std::clamp(start.right() + deltaAt(start.left() + start.width(), pixels.x()),
                                              start.left() + minimumWidth - 1, std::max(page.width() - 1, start.left() + minimumWidth - 1)));
        if (top) result.setTop(std::clamp(start.top() + deltaAt(start.top(), pixels.y()),
                                          0, std::max(0, start.top() + start.height() - minimumHeight)));
        if (bottom) result.setBottom(std::clamp(start.bottom() + deltaAt(start.top() + start.height(), pixels.y()),
                                                start.top() + minimumHeight - 1, std::max(page.height() - 1, start.top() + minimumHeight - 1)));
        return result;
    }
};

inline constexpr Grid ReportGrid{ReportUnitsPerSourcePixel};
inline constexpr Grid TemplateGrid{TemplateUnitsPerSourcePixel};

inline QRect unionOf(const DesignerSelection::BoundsChanges& frames)
{
    QRect result;
    for (const auto& entry : frames) result = result.united(entry.second);
    return result;
}

template <class Frames>
QRect unionOfFrames(const Frames& frames)
{
    QRect result;
    for (const auto& frame : frames) result = result.united(frame.bounds);
    return result;
}

} // namespace CardStack::DesignerInteraction
