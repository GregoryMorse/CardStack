#pragma once

#include <QApplication>
#include <QCoreApplication>

namespace CardStack {
inline void initializeApplicationAppearance(QApplication& app)
{
    QCoreApplication::setApplicationVersion(QStringLiteral(CARDSTACK_VERSION));
    app.setStyleSheet(QStringLiteral(R"(
        QPushButton {
            border: 1px solid #aeb7c2;
            border-radius: 6px;
            padding: 5px 12px;
            background-color: #f7f9fb;
        }
        QPushButton:hover {
            border-color: #78889a;
            background-color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #e5eaf0;
        }
        QPushButton:disabled {
            color: #929aa3;
            border-color: #d2d7dc;
            background-color: #eef1f4;
        }
        QPushButton:default {
            border-color: #557da5;
        }
    )"));
}
} // namespace CardStack
