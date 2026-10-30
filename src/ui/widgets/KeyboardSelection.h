#pragma once

#include <QKeyEvent>
#include <QVector>
#include <algorithm>

namespace CardStack {

// Navigation has a separate cursor: discontinuous selection must not select on movement.
class KeyboardSelection {
public:
    enum class Mode { None, Continuous, Discontinuous };
    enum class Command { None, Continuous, Discontinuous, Cancel, Next, Previous, First, Last, Toggle };
    struct Result { QVector<int> selected; int cursor = -1; bool range = false; };

    Mode mode() const { return m_mode; }
    int cursor() const { return m_cursor; }
    int anchor() const { return m_anchor; }
    void setPointerPosition(int anchor, int cursor) { m_anchor = anchor; m_cursor = cursor; }
    void reset() { m_mode = Mode::None; m_cursor = m_anchor = -1; }

    Command command(const QKeyEvent& key) const
    {
        if (key.modifiers() == Qt::NoModifier && key.key() == Qt::Key_F8)
            return m_mode == Mode::Continuous ? Command::Cancel : Command::Continuous;
        if (key.modifiers() == Qt::ShiftModifier && key.key() == Qt::Key_F8)
            return m_mode == Mode::Discontinuous ? Command::Cancel : Command::Discontinuous;
        if (m_mode == Mode::None) return Command::None;
        if ((key.key() == Qt::Key_Tab || key.key() == Qt::Key_Backtab)
            && key.modifiers() == Qt::ShiftModifier) return Command::Previous;
        if (key.modifiers() != Qt::NoModifier) return Command::None;
        switch (key.key()) {
        case Qt::Key_Escape: return Command::Cancel;
        case Qt::Key_Tab: case Qt::Key_Right: case Qt::Key_Down: return Command::Next;
        case Qt::Key_Backtab: case Qt::Key_Left: case Qt::Key_Up: return Command::Previous;
        case Qt::Key_Home: case Qt::Key_PageUp: return Command::First;
        case Qt::Key_End: case Qt::Key_PageDown: return Command::Last;
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
            return m_mode == Mode::Discontinuous ? Command::Toggle : Command::None;
        default: return Command::None;
        }
    }

    Result apply(Command action, const QVector<int>& order, QVector<int> selected, int current, bool wrap)
    {
        if (order.isEmpty()) { reset(); return {}; }
        if (m_mode == Mode::None || !order.contains(m_cursor))
            m_cursor = order.contains(current) ? current : order.first();
        if (action == Command::Cancel) {
            m_mode = Mode::None;
            return {std::move(selected), m_cursor, false};
        }
        if (action == Command::Continuous) {
            m_mode = Mode::Continuous;
            m_anchor = m_cursor;
        } else if (action == Command::Discontinuous) {
            m_mode = Mode::Discontinuous;
        } else if (action == Command::Toggle) {
            if (selected.contains(m_cursor)) selected.removeAll(m_cursor);
            else selected.append(m_cursor);
        } else {
            int position = int(order.indexOf(m_cursor));
            switch (action) {
            case Command::Next: ++position; break;
            case Command::Previous: --position; break;
            case Command::First: position = 0; break;
            case Command::Last: position = int(order.size()) - 1; break;
            default: break;
            }
            if (wrap) position = (position + int(order.size())) % int(order.size());
            m_cursor = order.at(std::clamp(position, 0, int(order.size()) - 1));
        }
        const bool range = m_mode == Mode::Continuous;
        if (range) {
            if (!order.contains(m_anchor)) m_anchor = m_cursor;
            const int first = std::min(int(order.indexOf(m_anchor)), int(order.indexOf(m_cursor)));
            const int last = std::max(int(order.indexOf(m_anchor)), int(order.indexOf(m_cursor)));
            selected = order.mid(first, last - first + 1);
        }
        return {std::move(selected), m_cursor, range};
    }

private:
    Mode m_mode = Mode::None;
    int m_cursor = -1;
    int m_anchor = -1;
};

} // namespace CardStack
