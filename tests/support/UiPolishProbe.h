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
    const QImage normal = render(*spin);
    const qreal scale = normal.devicePixelRatio();
    const auto pixelAt = [&](QPoint point) { return normal.pixelColor(qRound(point.x() * scale), qRound(point.y() * scale)); };
    const auto filledBase = [&](const QRect& half, bool up) {
        const int y = half.center().y() + (up ? 1 : -1);
        const QColor ink = pixelAt(QPoint(half.center().x(), y));
        const QColor background = pixelAt(QPoint(half.right() - 1, half.center().y()));
        if (qAbs(ink.red() - background.red()) + qAbs(ink.green() - background.green())
            + qAbs(ink.blue() - background.blue()) < 60) return 0;
        int count = 0;
        for (int x = half.center().x() - 2; x <= half.center().x() + 2; ++x) {
            const QColor actual = pixelAt(QPoint(x, y));
            if (qAbs(actual.red() - ink.red()) < 20 && qAbs(actual.green() - ink.green()) < 20
                && qAbs(actual.blue() - ink.blue()) < 20) ++count;
        }
        return count;
    };
    const int split = spin->height() / 2;
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
    saveArrow(QStringLiteral("arrows_hover"));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &press);
    QCOMPARE(spin->geometry(), original);
    saveArrow(QStringLiteral("arrows_pressed"));
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &release);
    spin->setEnabled(false);
    QCOMPARE(spin->geometry(), original);
    saveArrow(QStringLiteral("arrows_disabled"));
    qInfo() << "Micro-scroll geometry:" << original << "DPR:" << scale;
    QVERIFY(filledBase(QRect(0, 0, spin->width(), split), true) >= 4);
    QVERIFY(filledBase(QRect(0, split, spin->width(), spin->height() - split), false) >= 4);
}
} // namespace CardStack::Tests::UiPolishProbe
