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
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <functional>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QPlainTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFontInfo>
#include <QFile>
#include <QComboBox>
#include <QAbstractButton>

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

        // A regular-only application font hides installed bold/italic faces
        // on the offscreen backend. Load its companion faces for real chooser
        // and renderer coverage instead of silently testing Regular only.
        const QDir fontDirectory = QFileInfo(candidate).dir();
        QStringList companionFaces;
        const QString baseName = QFileInfo(candidate).fileName();
        if (baseName == QLatin1String("segoeui.ttf"))
            companionFaces = {QStringLiteral("segoeuib.ttf"), QStringLiteral("segoeuii.ttf"), QStringLiteral("segoeuiz.ttf")};
        else if (baseName == QLatin1String("arial.ttf"))
            companionFaces = {QStringLiteral("arialbd.ttf"), QStringLiteral("ariali.ttf"), QStringLiteral("arialbi.ttf")};
        else if (baseName == QLatin1String("DejaVuSans.ttf"))
            companionFaces = {QStringLiteral("DejaVuSans-Bold.ttf"), QStringLiteral("DejaVuSans-Oblique.ttf"), QStringLiteral("DejaVuSans-BoldOblique.ttf")};
        else if (baseName == QLatin1String("LiberationSans-Regular.ttf"))
            companionFaces = {QStringLiteral("LiberationSans-Bold.ttf"), QStringLiteral("LiberationSans-Italic.ttf"), QStringLiteral("LiberationSans-BoldItalic.ttf")};
        else if (baseName == QLatin1String("Arial.ttf"))
            companionFaces = {QStringLiteral("Arial Bold.ttf"), QStringLiteral("Arial Italic.ttf"), QStringLiteral("Arial Bold Italic.ttf")};
        for (const QString& face : companionFaces) {
            const QString path = fontDirectory.filePath(face);
            if (QFileInfo::exists(path)) QFontDatabase::addApplicationFont(path);
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
    if (!widget.isVisible()) widget.show();
    // Native test platforms are restricted to an isolated desktop by the test runner.
    QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();
    const QPixmap pixmap = widget.grab();
    if (pixmap.isNull() || !pixmap.save(outputDirectory.filePath(fileName))) return false;

    QJsonArray controls;
    for (QWidget* child : widget.findChildren<QWidget*>()) {
        QJsonObject state{{QStringLiteral("id"), child->property("originalControlId").toInt()},
            {QStringLiteral("objectName"), child->objectName()},
            {QStringLiteral("class"), QString::fromLatin1(child->metaObject()->className())},
            {QStringLiteral("enabled"), child->isEnabled()},
            {QStringLiteral("visible"), child->isVisibleTo(&widget)}};
        const QPoint point = child->mapTo(&widget, QPoint());
        state.insert(QStringLiteral("rect"), QJsonArray{point.x(), point.y(), child->width(), child->height()});
        QString text;
        if (auto* edit = qobject_cast<QLineEdit*>(child))
            text = edit->echoMode() == QLineEdit::Normal ? edit->text() : QStringLiteral("<redacted>");
        else if (auto* edit = qobject_cast<QPlainTextEdit*>(child)) text = edit->toPlainText();
        else if (auto* label = qobject_cast<QLabel*>(child)) text = label->text();
        if (auto* button = qobject_cast<QAbstractButton*>(child)) {
            text = button->text();
            state.insert(QStringLiteral("checked"), button->isChecked());
        }
        if (auto* combo = qobject_cast<QComboBox*>(child)) {
            text = combo->currentText();
            state.insert(QStringLiteral("selectedIndex"), combo->currentIndex());
            QJsonArray items;
            for (int i = 0; i < std::min(combo->count(), 1024); ++i) items.append(combo->itemText(i));
            state.insert(QStringLiteral("items"), items);
        }
        state.insert(QStringLiteral("text"), text.left(4096));
        const QFontInfo font(child->font());
        state.insert(QStringLiteral("font"), QJsonObject{{QStringLiteral("family"), font.family()},
            {QStringLiteral("requestedFamily"), child->font().family()},
            {QStringLiteral("requestedPixelSize"), child->font().pixelSize()},
            {QStringLiteral("pointSize"), font.pointSizeF()}, {QStringLiteral("pixelSize"), font.pixelSize()},
            {QStringLiteral("weight"), int(font.weight())}, {QStringLiteral("italic"), font.italic()}});
        controls.append(state);
    }
    QString key = QFileInfo(fileName).completeBaseName();
    key.remove(QRegularExpression(QStringLiteral("^\\d+_")));
    const QRegularExpression suffix(QStringLiteral("^(.+)_(initial|wide|and_variant|or_variant|dropdown_[0-9]+)$"));
    const auto match = suffix.match(key);
    if (match.hasMatch()) key = match.captured(1) + QLatin1Char('.') + match.captured(2);
    QJsonArray menus;
    if (auto* menuBar = widget.findChild<QMenuBar*>()) {
        std::function<void(const QList<QAction*>&, const QString&)> appendMenus;
        appendMenus = [&](const QList<QAction*>& actions, const QString& prefix) {
            for (QAction* action : actions) {
                const QString path = prefix + QLatin1Char('/') + action->text();
                menus.append(QJsonObject{{QStringLiteral("id"), action->data().toInt()},
                    {QStringLiteral("path"), path}, {QStringLiteral("enabled"), action->isEnabled()},
                    {QStringLiteral("checked"), action->isChecked()}});
                if (action->menu()) appendMenus(action->menu()->actions(), path);
            }
        };
        appendMenus(menuBar->actions(), {});
    }
    const QJsonObject record{{QStringLiteral("version"), 1}, {QStringLiteral("key"), key},
        {QStringLiteral("image"), fileName}, {QStringLiteral("title"), widget.windowTitle()},
        {QStringLiteral("dialog"), widget.property("legacyDialogName").toString()},
        {QStringLiteral("platform"), QGuiApplication::platformName()}, {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("devicePixelRatio"), widget.devicePixelRatioF()},
        {QStringLiteral("logicalDpi"), widget.logicalDpiX()},
        {QStringLiteral("stylesheetSha256"), QString::fromLatin1(QCryptographicHash::hash(qApp->styleSheet().toUtf8(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("width"), widget.width()}, {QStringLiteral("height"), widget.height()},
        {QStringLiteral("controls"), controls}, {QStringLiteral("menus"), menus}};
    QFile metadata(outputDirectory.filePath(fileName + QStringLiteral(".json")));
    if (!metadata.open(QIODevice::WriteOnly)) return false;
    return metadata.write(QJsonDocument(record).toJson(QJsonDocument::Indented)) > 0;
}

} // namespace CardStack::Tests
