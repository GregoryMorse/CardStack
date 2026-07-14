#pragma once

#include "Deck.h"

#include <QColor>
#include <QPalette>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace CardStack {

inline QColor deckSystemColor(DeckColorRole role, const QPalette& palette)
{
#ifdef Q_OS_WIN
    int systemColorIndex = COLOR_WINDOWTEXT;
    switch (role) {
    case DeckColorRole::IndexForeground:
        systemColorIndex = COLOR_BTNTEXT;
        break;
    case DeckColorRole::DataForeground:
    case DeckColorRole::TextForeground:
        systemColorIndex = COLOR_WINDOWTEXT;
        break;
    case DeckColorRole::NameForeground:
        systemColorIndex = COLOR_MENUTEXT;
        break;
    case DeckColorRole::DataBackground:
        systemColorIndex = COLOR_WINDOW;
        break;
    case DeckColorRole::IndexBackground:
        systemColorIndex = COLOR_BTNFACE;
        break;
    case DeckColorRole::CardBackground:
        systemColorIndex = COLOR_MENU;
        break;
    default:
        break;
    }
    const COLORREF color = GetSysColor(systemColorIndex);
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
#else
    switch (role) {
    case DeckColorRole::IndexForeground:
        return palette.color(QPalette::ButtonText);
    case DeckColorRole::DataForeground:
        return palette.color(QPalette::Text);
    case DeckColorRole::NameForeground:
    case DeckColorRole::TextForeground:
        return palette.color(QPalette::WindowText);
    case DeckColorRole::IndexBackground:
        return palette.color(QPalette::Button);
    case DeckColorRole::DataBackground:
        return palette.color(QPalette::Base);
    case DeckColorRole::CardBackground:
        return palette.color(QPalette::Window);
    default:
        return palette.color(QPalette::WindowText);
    }
#endif
}

} // namespace CardStack
