#pragma once

#include <QPointF>
#include <QRectF>
#include <Qt>
#include <algorithm>
#include <array>

namespace CardStack::DesignerResize {

enum class Handle { None, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

inline constexpr qreal HandleSizePx = 7.0;
inline constexpr qreal HitPaddingPx = 3.0;
inline constexpr qreal MinimumVisualExtentPx = 8.0;
inline constexpr std::array<Handle, 8> Handles = {
    Handle::TopLeft, Handle::TopRight, Handle::BottomLeft, Handle::BottomRight,
    Handle::Left, Handle::Right, Handle::Top, Handle::Bottom,
};

inline QRectF handleRect(const QRectF& frame, Handle handle)
{
    qreal x = frame.center().x();
    qreal y = frame.center().y();
    if (handle == Handle::Left || handle == Handle::TopLeft || handle == Handle::BottomLeft) x = frame.left() - HandleSizePx / 2.0;
    if (handle == Handle::Right || handle == Handle::TopRight || handle == Handle::BottomRight) x = frame.right() + HandleSizePx / 2.0;
    if (handle == Handle::Top || handle == Handle::TopLeft || handle == Handle::TopRight) y = frame.top() - HandleSizePx / 2.0;
    if (handle == Handle::Bottom || handle == Handle::BottomLeft || handle == Handle::BottomRight) y = frame.bottom() + HandleSizePx / 2.0;
    return QRectF(x - HandleSizePx / 2.0, y - HandleSizePx / 2.0, HandleSizePx, HandleSizePx);
}

inline Handle hitTest(const QRectF& frame, const QPointF& point)
{
    for (Handle handle : Handles) {
        if (handleRect(frame, handle).adjusted(-HitPaddingPx, -HitPaddingPx, HitPaddingPx, HitPaddingPx).contains(point))
            return handle;
    }
    return Handle::None;
}

inline Qt::CursorShape cursorFor(Handle handle)
{
    switch (handle) {
    case Handle::Left: case Handle::Right: return Qt::SizeHorCursor;
    case Handle::Top: case Handle::Bottom: return Qt::SizeVerCursor;
    case Handle::TopLeft: case Handle::BottomRight: return Qt::SizeFDiagCursor;
    case Handle::TopRight: case Handle::BottomLeft: return Qt::SizeBDiagCursor;
    case Handle::None: return Qt::ArrowCursor;
    }
    return Qt::ArrowCursor;
}

inline QRectF resizedRect(const QRectF& start, const QPointF& delta, Handle handle, const QRectF& page)
{
    QRectF result = start;
    const qreal minimumWidth = std::min(MinimumVisualExtentPx, start.width());
    const qreal minimumHeight = std::min(MinimumVisualExtentPx, start.height());
    if (handle == Handle::Left || handle == Handle::TopLeft || handle == Handle::BottomLeft) {
        const qreal limit = start.right() - minimumWidth;
        result.setLeft(std::clamp(start.left() + delta.x(), std::min(page.left(), limit), limit));
    }
    if (handle == Handle::Right || handle == Handle::TopRight || handle == Handle::BottomRight) {
        const qreal limit = start.left() + minimumWidth;
        result.setRight(std::clamp(start.right() + delta.x(), limit, std::max(page.right(), limit)));
    }
    if (handle == Handle::Top || handle == Handle::TopLeft || handle == Handle::TopRight) {
        const qreal limit = start.bottom() - minimumHeight;
        result.setTop(std::clamp(start.top() + delta.y(), std::min(page.top(), limit), limit));
    }
    if (handle == Handle::Bottom || handle == Handle::BottomLeft || handle == Handle::BottomRight) {
        const qreal limit = start.top() + minimumHeight;
        result.setBottom(std::clamp(start.bottom() + delta.y(), limit, std::max(page.bottom(), limit)));
    }
    return result;
}

} // namespace CardStack::DesignerResize
