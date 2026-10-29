#pragma once

#include "UiBuilder.h"
#include "UiIds.h"
#include "ReportFormGeometry.h"
#include "NativeTestSurface.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>

namespace CardStack::Tests::UiPolishProbe {
inline void capture(QWidget& widget, const QString& name)
{
    const QString output = qEnvironmentVariable("CARDSTACK_UI_POLISH_GALLERY_DIR");
    if (output.isEmpty()) return;
    prepareNativeTestSurface(widget);
    const QString expectedDpr = qEnvironmentVariable("CARDSTACK_EXPECTED_DPR");
    if (!expectedDpr.isEmpty()) QVERIFY2(qAbs(widget.devicePixelRatioF() - expectedDpr.toDouble()) < 0.01,
        qPrintable(QStringLiteral("DPR requested %1, actual %2").arg(expectedDpr).arg(widget.devicePixelRatioF())));
    QDir directory(output);
    QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
    QCoreApplication::processEvents();
    QVERIFY(widget.grab().save(directory.filePath(name + QStringLiteral(".png"))));
    QFile title(directory.filePath(name + QStringLiteral(".title.txt")));
    QVERIFY(title.open(QIODevice::WriteOnly | QIODevice::Truncate));
    title.write(widget.windowTitle().toUtf8());
}

inline void helpTitles()
{
    int checked = 0;
    for (const QString& name : UiBuilder::dialogNames()) {
        auto parent = UiBuilder::createDialog(name);
        QVERIFY(parent);
        auto* help = qobject_cast<QAbstractButton*>(UiBuilder::controlById(parent.get(), UiIds::Control::Help));
        if (!help || help->isHidden()) continue;
        prepareNativeTestSurface(*parent);
        QString readableTitle = parent->windowTitle();
        readableTitle.remove(QLatin1Char('&'));
        if (readableTitle == name) readableTitle.clear();
        const QString expected = readableTitle.isEmpty() ? QObject::tr("CardStack Help")
            : QObject::tr("%1 Help").arg(readableTitle);
        bool seen = false;
        QString actual;
        bool hasBrowser = false;
        QTimer handler, timeout;
        handler.setInterval(0);
        timeout.setSingleShot(true);
        QObject::connect(&handler, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != QStringLiteral("cardstackHelpDialog")) return;
            handler.stop();
            timeout.stop();
            seen = true;
            actual = dialog->windowTitle();
            auto* browser = dialog->findChild<QTextBrowser*>();
            hasBrowser = browser && !browser->toPlainText().trimmed().isEmpty();
            if (name == QStringLiteral("SEARCH")) capture(*dialog, QStringLiteral("help_find"));
            if (name == QStringLiteral("DEFINEFORM")) capture(*dialog, QStringLiteral("help_custom_form"));
            if (name == QStringLiteral("PHNDEF")) capture(*dialog, QStringLiteral("help_phone_dialer"));
            if (name == QStringLiteral("TEXTFRAME")) capture(*dialog, QStringLiteral("help_text_frame"));
            dialog->reject();
        });
        QObject::connect(&timeout, &QTimer::timeout, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        handler.start();
        timeout.start(3000);
        help->click();
        handler.stop();
        timeout.stop();
        QVERIFY2(seen, qPrintable(QStringLiteral("Help did not open for %1").arg(name)));
        QCOMPARE(actual, expected);
        QVERIFY(hasBrowser);
        QVERIFY(!qobject_cast<QDialog*>(QApplication::activeModalWidget()));
        ++checked;
    }
    QVERIFY(checked > 10);
    qInfo() << "Context help titles checked:" << checked;
}

inline QImage render(QWidget& widget)
{
    const qreal dpr = widget.devicePixelRatioF();
    QImage image(QSize(qCeil(widget.width() * dpr), qCeil(widget.height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
}

inline void customFormKeyboardHelp()
{
    auto parent = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"));
    QVERIFY(parent);
    prepareNativeTestSurface(*parent);
    parent->show();
    QApplication::setActiveWindow(parent.get());
    parent->setFocus();
    QCoreApplication::processEvents();
    bool seen = false;
    QString title, content;
    QTimer handler, timeout;
    handler.setInterval(0);
    timeout.setSingleShot(true);
    QObject::connect(&handler, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("cardstackHelpDialog")) return;
        handler.stop();
        timeout.stop();
        seen = true;
        title = dialog->windowTitle();
        if (auto* browser = dialog->findChild<QTextBrowser*>()) content = browser->toPlainText();
        capture(*dialog, QStringLiteral("help_custom_form"));
        dialog->reject();
    });
    QObject::connect(&timeout, &QTimer::timeout, [] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
    });
    handler.start();
    timeout.start(3000);
    QTest::keyClick(parent.get(), Qt::Key_F1);
    QTRY_VERIFY_WITH_TIMEOUT(seen, 3500);
    QCOMPARE(title, QObject::tr("%1 Help").arg(parent->windowTitle()));
    QVERIFY(content.contains(QStringLiteral("Define Custom Form")));
    parent->close();
}

inline void formGeometry()
{
    const auto geometry = reportFormGeometry(QSize(8500, 11000), QMargins(500, 500, 500, 500),
                                             QSize(250, 250), 5, 2);
    QVERIFY(geometry.valid());
    QCOMPARE(geometry.formWidth, 3625);
    QCOMPARE(geometry.formHeight, 1800);
    QCOMPARE(geometry.cell(4, 1), QRect(4375, 8700, 3625, 1800));
    QVERIFY(!reportFormGeometry(QSize(1000, 1000), QMargins(600, 0, 600, 0), QSize(), 1, 1).valid());
}

inline void microScroll()
{
    auto dialog = UiBuilder::createDialog(QStringLiteral("DEFINEFORM"));
    QVERIFY(dialog);
    prepareNativeTestSurface(*dialog);
    dialog->show();
    QCoreApplication::processEvents();
    auto* spin = qobject_cast<QSpinBox*>(UiBuilder::controlById(dialog.get(), UiIds::Control::DefineFormWidthSpin));
    QVERIFY(spin);
    const QRect original = spin->geometry();
    QVERIFY(original.width() > 0 && original.height() > 0);
    QCOMPARE(spin->sizeHint(), QSize(10, 14));
    QCOMPARE(spin->minimumSizeHint(), QSize(8, 12));
    const qreal scale = spin->devicePixelRatioF();
    const auto verifyCenteredArrows = [&] {
        constexpr int MinimumInkContrast = 60;
        const QImage image = render(*spin);
        const int split = spin->height() / 2;
        const int inset = qCeil(scale);
        const QColor background = image.pixelColor(inset, inset);
        for (bool up : {true, false}) {
            const int top = up ? 0 : qRound(split * scale);
            const int bottom = up ? qRound(split * scale) : image.height();
            const QRect interior(inset, top + inset, image.width() - 2 * inset,
                                 bottom - top - 2 * inset);
            const QColor foreground = image.pixelColor(image.width() / 2, (top + bottom) / 2);
            const int contrast = qAbs(foreground.red() - background.red())
                + qAbs(foreground.green() - background.green()) + qAbs(foreground.blue() - background.blue());
            QVERIFY(contrast >= MinimumInkContrast);
            QRect ink;
            QVector<int> rowCounts;
            for (int y = interior.top(); y <= interior.bottom(); ++y) {
                int count = 0;
                for (int x = interior.left(); x <= interior.right(); ++x) {
                    const QColor color = image.pixelColor(x, y);
                    // Exclude the frame/separator, including partially covered DPI edge pixels.
                    if (color != foreground) continue;
                    ink = ink.united(QRect(x, y, 1, 1));
                    ++count;
                }
                if (count != 0) rowCounts.append(count);
            }
            const QString detail = QStringLiteral("%1 arrow: widget %2x%3, DPR %4, ink %5,%6 %7x%8")
                .arg(up ? QStringLiteral("up") : QStringLiteral("down"))
                .arg(spin->width()).arg(spin->height()).arg(scale)
                .arg(ink.x()).arg(ink.y()).arg(ink.width()).arg(ink.height());
            QVERIFY2(!ink.isEmpty(), qPrintable(detail));
            // Fractional-DPI raster rounding permits at most half a physical pixel.
            QVERIFY2(qAbs(ink.left() + ink.right() + 1 - spin->width() * scale) <= 1.0, qPrintable(detail));
            const qreal buttonCenterTwice = (up ? split : split + spin->height()) * scale;
            QVERIFY2(qAbs(ink.top() + ink.bottom() + 1 - buttonCenterTwice) <= 1.0, qPrintable(detail));
            QVERIFY2(ink.width() >= qFloor(4 * scale), qPrintable(detail));
            QVERIFY2(ink.height() >= qFloor(2 * scale), qPrintable(detail));
            QVERIFY2(ink.width() <= qRound(6 * scale), qPrintable(detail));
            QVERIFY2(ink.height() <= qRound(4 * scale), qPrintable(detail));
            QCOMPARE(rowCounts.size(), ink.height());
            for (int row = 1; row < rowCounts.size(); ++row) {
                QVERIFY2(up ? rowCounts[row] >= rowCounts[row - 1]
                            : rowCounts[row] <= rowCounts[row - 1], qPrintable(detail));
            }
        }
    };
    verifyCenteredArrows();
    const QImage normal = render(*spin);
    const QFont originalFont = spin->font();
    QFont enlargedFont = originalFont;
    enlargedFont.setPixelSize(40);
    spin->setFont(enlargedFont);
    QCOMPARE(spin->geometry(), original);
    QCOMPARE(render(*spin), normal);
    spin->setFont(originalFont);
    // Synthetic paint surfaces only; restore the production geometry before capture.
    for (const QSize& size : {QSize(8, 12), QSize(10, 14), QSize(11, 25), QSize(14, 32)}) {
        spin->resize(size);
        verifyCenteredArrows();
    }
    spin->setGeometry(original);
    QCOMPARE(spin->geometry(), original);
    capture(*spin, QStringLiteral("micro_scroll_control"));
    const auto saveArrow = [&](const QString& state) {
        const QString output = qEnvironmentVariable("CARDSTACK_UI_POLISH_GALLERY_DIR");
        if (output.isEmpty()) return;
        const QImage image = render(*spin);
        QVERIFY(image.save(QDir(output).filePath(state + QStringLiteral(".png"))));
        QVERIFY(image.scaled(image.size() * 8, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                    .save(QDir(output).filePath(state + QStringLiteral("_8x.png"))));
    };
    saveArrow(QStringLiteral("arrows_normal"));
    const QPointF local(spin->rect().center());
    const QPointF global(spin->mapToGlobal(local.toPoint()));
    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &move);
    QCOMPARE(spin->geometry(), original);
    verifyCenteredArrows();
    saveArrow(QStringLiteral("arrows_hover"));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &press);
    QCOMPARE(spin->geometry(), original);
    verifyCenteredArrows();
    saveArrow(QStringLiteral("arrows_pressed"));
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &release);
    spin->setEnabled(false);
    QCOMPARE(spin->geometry(), original);
    verifyCenteredArrows();
    saveArrow(QStringLiteral("arrows_disabled"));
    qInfo() << "Micro-scroll geometry:" << original << "DPR:" << scale;
}
} // namespace CardStack::Tests::UiPolishProbe
