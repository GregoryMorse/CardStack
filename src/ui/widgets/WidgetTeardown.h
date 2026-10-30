#pragma once

#include <QApplication>
#include <QObject>
#include <QWidget>

namespace CardStack::Detail {

// Child widgets are destroyed by QWidget after derived document members are gone.
inline void prepareWidgetTeardown(QWidget& owner)
{
    if (qApp) {
        qApp->removeEventFilter(&owner);
        QObject::disconnect(qApp, nullptr, &owner, nullptr);
    }
    for (QObject* child : owner.findChildren<QObject*>()) {
        child->blockSignals(true);
        QObject::disconnect(child, nullptr, &owner, nullptr);
    }
}

} // namespace CardStack::Detail
