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

    // Native Qt already has complete font families, including bold and italic faces.
    // Registering only a regular face can shadow those installed variants.
    if (QGuiApplication::platformName() != QLatin1String("offscreen")
        && !qEnvironmentVariableIsSet("CARDSTACK_VISUAL_TEST_FONT")) return;

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

        // Native gallery captures retain the production font. Offscreen builds
        // only need a fallback if the platform supplied no installed family.
        const QString current = QApplication::font().family();
        if (!QFontDatabase::families().contains(current, Qt::CaseInsensitive)
            && qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen"))
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
