#pragma once

#include <cstddef>
#include <cstdint>

namespace CardStack::UiResourceData {

struct UiMenuItem {
    int id;
    const char* text;
    bool separator;
    const UiMenuItem* children;
    std::size_t childCount;
};

struct UiMenu {
    int name;
    const UiMenuItem* items;
    std::size_t itemCount;
};

struct UiControl {
    const char* className;
    const char* text;
    int id;
    std::uint32_t style;
    int x;
    int y;
    int width;
    int height;
};

struct UiDialog {
    const char* name;
    const char* title;
    int x;
    int y;
    int width;
    int height;
    const char* fontTypeface;
    int fontPointSize;
    const UiControl* controls;
    std::size_t controlCount;
};

struct UiString {
    int id;
    const char* text;
};

const UiMenu* findMenu(int menuId);
const UiDialog* findDialog(const char* name);
const UiString* findString(int stringId);
const UiMenu* menus();
std::size_t menuCount();
const UiDialog* dialogs();
std::size_t dialogCount();
const UiString* strings();
std::size_t stringCount();

} // namespace CardStack::UiResourceData
