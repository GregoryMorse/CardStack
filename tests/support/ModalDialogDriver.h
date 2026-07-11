#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

namespace CardStack::Tests {

inline bool clickMessageBoxButton(QMessageBox::StandardButton button)
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget->isVisible()) {
            continue;
        }

        auto* messageBox = qobject_cast<QMessageBox*>(widget);
        if (messageBox == nullptr) {
            continue;
        }

        QAbstractButton* abstractButton = messageBox->button(button);
        if (abstractButton == nullptr) {
            continue;
        }

        abstractButton->click();
        return true;
    }

    return false;
}

inline void chooseNextMessageBoxButton(QMessageBox::StandardButton button)
{
    auto attemptsRemaining = std::make_shared<int>(50);
    auto retry = std::make_shared<std::function<void()>>();
    *retry = [button, attemptsRemaining, retry]() {
        if (clickMessageBoxButton(button)) {
            return;
        }

        --(*attemptsRemaining);
        if (*attemptsRemaining > 0) {
            QTimer::singleShot(10, qApp, *retry);
        }
    };

    QTimer::singleShot(0, qApp, *retry);
}

} // namespace CardStack::Tests
