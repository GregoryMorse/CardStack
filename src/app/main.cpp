#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QSize>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CardStack"));
    QCoreApplication::setApplicationName(QStringLiteral("CardStack"));

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

    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-16.png"), QSize(16, 16));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-24.png"), QSize(24, 24));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-32.png"), QSize(32, 32));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-48.png"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-64.png"), QSize(64, 64));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-128.png"), QSize(128, 128));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-256.png"), QSize(256, 256));
    app.setWindowIcon(appIcon);

    CardStack::MainWindow mainWindow(nullptr, false, true);
    mainWindow.setWindowIcon(appIcon);
    if (!mainWindow.property("cardstackMainGeometryRestored").toBool()) {
        mainWindow.resize(1100, 720);
    }
    mainWindow.show();

    return app.exec();
}
