#include "MainWindow.h"
#include "ApplicationAppearance.h"

#include <QApplication>
#include <QIcon>
#include <QSvgRenderer>
#include <QSize>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(cardstack_app);
    QCoreApplication::setOrganizationName(QStringLiteral("CardStack"));
    QCoreApplication::setApplicationName(QStringLiteral("CardStack"));

    CardStack::initializeApplicationAppearance(app);

    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-16.png"), QSize(16, 16));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-24.png"), QSize(24, 24));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-32.png"), QSize(32, 32));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-48.png"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-64.png"), QSize(64, 64));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-128.png"), QSize(128, 128));
    appIcon.addFile(QStringLiteral(":/cardstack/icons/icon-256.png"), QSize(256, 256));
    app.setWindowIcon(appIcon);

    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        // Exercise the deployed plugins/assets without creating a main window or restoring a session.
        // Keep SVG support a direct runtime dependency so deployment tools include its icon plugins.
        QSvgRenderer svgIcon(QStringLiteral(":/cardstack/window-icons/deck.svg"));
        if (!svgIcon.isValid()) {
            std::fputs("CardStack smoke test: SVG renderer is unavailable.\n", stderr);
            return 2;
        }
        if (appIcon.pixmap(32, 32).isNull()) {
            std::fputs("CardStack smoke test: application icon is unavailable.\n", stderr);
            return 2;
        }
        QStringList pending{QStringLiteral(":/")};
        int checkedIcons = 0;
        while (!pending.isEmpty()) {
            const QDir directory(pending.takeLast());
            for (const QString& name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString path = directory.filePath(name);
                if (!path.startsWith(QStringLiteral(":/qt-project.org")) && !path.startsWith(QStringLiteral(":/qt/")))
                    pending.append(path);
            }
            for (const QString& name : directory.entryList({QStringLiteral("*.png"), QStringLiteral("*.svg")}, QDir::Files)) {
                const QString path = directory.filePath(name);
                if (QIcon(path).pixmap(24, 24).isNull()) {
                    std::fprintf(stderr, "CardStack smoke test: icon unavailable: %s\n", path.toUtf8().constData());
                    return 2;
                }
                ++checkedIcons;
            }
        }
        QWidget widget;
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        auto* layout = new QHBoxLayout(&widget);
        auto* logo = new QLabel(&widget);
        logo->setPixmap(appIcon.pixmap(32, 32));
        layout->addWidget(logo);
        layout->addWidget(new QLineEdit(QStringLiteral("CardStack"), &widget));
        widget.resize(320, 80);
        widget.ensurePolished();
        layout->activate();
        if (widget.grab().isNull()) {
            std::fputs("CardStack smoke test: widget rendering failed.\n", stderr);
            return 3;
        }
        const QString connection = QStringLiteral("cardstack-startup-smoke");
        bool sqlOk = false;
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(QStringLiteral(":memory:"));
            if (database.open()) {
                QSqlQuery query(database);
                sqlOk = query.exec(QStringLiteral("CREATE TABLE smoke(value TEXT NOT NULL)"))
                    && query.exec(QStringLiteral("INSERT INTO smoke VALUES('')"))
                    && query.exec(QStringLiteral("SELECT COUNT(*) FROM smoke"))
                    && query.next() && query.value(0).toInt() == 1;
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        if (!sqlOk) {
            std::fputs("CardStack smoke test: SQLite plugin failed.\n", stderr);
            return 4;
        }
        std::fprintf(stdout, "CardStack startup smoke test passed (%d packaged icons).\n", checkedIcons);
        return 0;
    }

    CardStack::MainWindow mainWindow(nullptr, false, true);
    mainWindow.setWindowIcon(appIcon);
    if (!mainWindow.property("cardstackMainGeometryRestored").toBool()) {
        mainWindow.resize(1100, 720);
    }
    mainWindow.show();

    return app.exec();
}
