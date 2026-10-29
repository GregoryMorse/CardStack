#pragma once

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

namespace CardStack::Tests {
inline void prepareNativeTestSurface(QWidget& widget)
{
    QWidget* window = widget.window();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        window->winId();
        if (QWindow* handle = window->windowHandle()) handle->setScreen(screen);
        window->move(screen->availableGeometry().topLeft() + QPoint(24, 24));
    }
}
} // namespace CardStack::Tests
