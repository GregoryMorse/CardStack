#pragma once

#include "KeyboardSelection.h"
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <algorithm>
#include <utility>

namespace CardStack::DesignerSelection {

using BoundsChanges = QVector<QPair<int, QRect>>;

inline QVector<int> normalized(QVector<int> indices, int frameCount)
{
    QVector<int> result;
    for (int index : indices) {
        if (index >= 0 && index < frameCount && !result.contains(index)) result.append(index);
    }
    return result;
}

template <class Frames>
QVector<int> positionalRange(const Frames& frames, int anchor, int target)
{
    if (anchor < 0 || anchor >= frames.size()) anchor = target;
    const auto position = [&frames](int index) {
        return std::pair(frames.at(index).bounds.top(), frames.at(index).bounds.left());
    };
    const auto first = std::min(position(anchor), position(target));
    const auto last = std::max(position(anchor), position(target));
    QVector<int> result;
    for (int index = 0; index < frames.size(); ++index) {
        if (first <= position(index) && position(index) <= last) result.append(index);
    }
    return result;
}

// Equal-position frames share a range endpoint; navigation skips coordinate ties.
template <class Frames>
QVector<int> positionalOrder(const Frames& frames, int current)
{
    QVector<int> order;
    for (int index = 0; index < frames.size(); ++index) order.append(index);
    const auto position = [&frames](int index) {
        return std::pair(frames.at(index).bounds.top(), frames.at(index).bounds.left());
    };
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        if (position(left) != position(right)) return position(left) < position(right);
        return left == current && right != current;
    });
    order.erase(std::unique(order.begin(), order.end(), [&](int left, int right) {
        return position(left) == position(right);
    }), order.end());
    return order;
}

inline BoundsChanges translated(const BoundsChanges& source, QPoint delta, QSize pageSize)
{
    QRect bounds;
    for (const auto& entry : source) bounds = bounds.united(entry.second);
    if (source.isEmpty()) return {};
    const int minimumX = -bounds.left();
    const int minimumY = -bounds.top();
    const int maximumX = std::max(minimumX, pageSize.width() - bounds.width() - bounds.left());
    const int maximumY = std::max(minimumY, pageSize.height() - bounds.height() - bounds.top());
    delta.setX(std::clamp(delta.x(), minimumX, maximumX));
    delta.setY(std::clamp(delta.y(), minimumY, maximumY));
    BoundsChanges result;
    for (const auto& entry : source) result.append({entry.first, entry.second.translated(delta)});
    return result;
}

} // namespace CardStack::DesignerSelection
