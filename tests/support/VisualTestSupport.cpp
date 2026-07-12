#include "VisualTestSupport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QWidget>

namespace CardStack::Tests {

void installVisualTestFonts()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    QStringList candidates;
    if (qEnvironmentVariableIsSet("CARDSTACK_VISUAL_TEST_FONT")) {
        candidates.append(qEnvironmentVariable("CARDSTACK_VISUAL_TEST_FONT"));
    }
    candidates.append(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    candidates.append(QStringLiteral("C:/Windows/Fonts/arial.ttf"));
    candidates.append(QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"));
    candidates.append(QStringLiteral("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"));
    candidates.append(QStringLiteral("/System/Library/Fonts/Supplemental/Arial.ttf"));
    candidates.append(QStringLiteral("/System/Library/Fonts/SFNS.ttf"));

    for (const QString& candidate : candidates) {
        if (!QFileInfo::exists(candidate)) {
            continue;
        }

        const int fontId = QFontDatabase::addApplicationFont(candidate);
        if (fontId < 0) {
            continue;
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty()) {
            continue;
        }

        QApplication::setFont(QFont(families.first(), 9));
        return;
    }
}

bool saveWidgetImage(QWidget& widget, const QDir& outputDirectory, const QString& fileName)
{
    installVisualTestFonts();
    if (!widget.isVisible()) {
        widget.show();
    }
    widget.raise();
    widget.activateWindow();
    QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();

    const QPixmap pixmap = widget.grab();
    return !pixmap.isNull() && pixmap.save(outputDirectory.filePath(fileName));
}

} // namespace CardStack::Tests
