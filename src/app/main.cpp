#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QSize>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-16.png"), QSize(16, 16));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-24.png"), QSize(24, 24));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-32.png"), QSize(32, 32));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-48.png"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-64.png"), QSize(64, 64));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-128.png"), QSize(128, 128));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-256.png"), QSize(256, 256));
    app.setWindowIcon(appIcon);

    CardStack::MainWindow mainWindow(nullptr, false);
    mainWindow.setWindowIcon(appIcon);
    mainWindow.resize(1100, 720);
    mainWindow.show();

    return app.exec();
}
