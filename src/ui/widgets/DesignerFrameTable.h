#pragma once

#include <QKeyEvent>
#include <QTableWidget>
#include <functional>

namespace CardStack {

// Preserve the same activation keys regardless of the platform's native item-view policy.
class DesignerFrameTable final : public QTableWidget {
public:
    explicit DesignerFrameTable(QWidget* parent = nullptr) : QTableWidget(parent) {}

    std::function<bool(QEvent*)> selectionKeyHandler;
    std::function<void()> selectionCancelled;

protected:
    bool event(QEvent* event) override
    {
        if (selectionKeyHandler && selectionKeyHandler(event)) return true;
        return QTableWidget::event(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (selectionCancelled) selectionCancelled();
        QTableWidget::mousePressEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        constexpr auto CommandModifiers = Qt::ShiftModifier | Qt::ControlModifier
            | Qt::AltModifier | Qt::MetaModifier;
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && !(event->modifiers() & CommandModifiers)) {
            const QModelIndex current = currentIndex();
            if (current.isValid() && (current.flags() & Qt::ItemIsEnabled))
                emit activated(current);
            event->accept();
            return;
        }
        QTableWidget::keyPressEvent(event);
    }
};

} // namespace CardStack
