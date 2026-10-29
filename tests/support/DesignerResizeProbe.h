#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QMouseEvent>
#include <QPixmap>
#include <QTest>
#include <QWidget>
#include <algorithm>

namespace CardStack::Tests::DesignerResizeProbe {

inline void sendPointer(QWidget* target, QEvent::Type type, const QPoint& local,
                        Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(local), QPointF(target->mapToGlobal(local)),
                      button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
}

inline void addData()
{
    QTest::addColumn<int>("horizontalEdge");
    QTest::addColumn<int>("verticalEdge");
    QTest::newRow("left") << -1 << 0;
    QTest::newRow("right") << 1 << 0;
    QTest::newRow("top") << 0 << -1;
    QTest::newRow("bottom") << 0 << 1;
    QTest::newRow("top_left") << -1 << -1;
    QTest::newRow("top_right") << 1 << -1;
    QTest::newRow("bottom_left") << -1 << 1;
    QTest::newRow("bottom_right") << 1 << 1;
}

template <class Designer, class ReadBounds>
void exercise(Designer& designer, const char* canvasName, QSize modelSize,
              const QRect& original, int rulerThickness, const QString& imagePrefix, ReadBounds readBounds)
{
    QFETCH(int, horizontalEdge);
    QFETCH(int, verticalEdge);
    designer.setAttribute(Qt::WA_DontShowOnScreen);
    designer.resize(1000, 700);
    designer.show();
    QCoreApplication::processEvents();
    QWidget* canvas = designer.template findChild<QWidget*>(QString::fromLatin1(canvasName));
    QVERIFY(canvas != nullptr);

    const QRectF available = canvas->rect().adjusted(rulerThickness + 16, rulerThickness + 16, -20, -20);
    const qreal aspect = static_cast<qreal>(modelSize.width()) / modelSize.height();
    qreal pageWidth = available.width();
    qreal pageHeight = pageWidth / aspect;
    if (pageHeight > available.height()) {
        pageHeight = available.height();
        pageWidth = pageHeight * aspect;
    }
    const QVariant document = canvas->property("designerPageRect");
    const QRectF page = document.isValid() ? document.toRectF()
        : QRectF(available.center().x() - pageWidth / 2.0,
                 available.center().y() - pageHeight / 2.0, pageWidth, pageHeight);
    const QRectF visual(page.left() + original.left() * page.width() / modelSize.width(),
                        page.top() + original.top() * page.height() / modelSize.height(),
                        original.width() * page.width() / modelSize.width(),
                        original.height() * page.height() / modelSize.height());
    const QPoint center = visual.center().toPoint();
    sendPointer(canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    sendPointer(canvas, QEvent::MouseMove, center, Qt::NoButton, Qt::LeftButton);
    sendPointer(canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(readBounds(), original);
    QVERIFY(!designer.canUndo());

    const QPoint start(qRound(horizontalEdge < 0 ? visual.left() : horizontalEdge > 0 ? visual.right() : visual.center().x()),
                       qRound(verticalEdge < 0 ? visual.top() : verticalEdge > 0 ? visual.bottom() : visual.center().y()));
    sendPointer(canvas, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
    const Qt::CursorShape expectedCursor = horizontalEdge == 0 ? Qt::SizeVerCursor
        : verticalEdge == 0 ? Qt::SizeHorCursor
        : horizontalEdge == verticalEdge ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    QCOMPARE(canvas->cursor().shape(), expectedCursor);
    const QPoint end = start + QPoint(horizontalEdge * 18, verticalEdge * 14);
    sendPointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendPointer(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    sendPointer(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    const QRect resized = readBounds();
    if (horizontalEdge != 0) QVERIFY(resized.width() > original.width());
    else QVERIFY(qAbs(resized.width() - original.width()) <= 2);
    if (verticalEdge != 0) QVERIFY(resized.height() > original.height());
    else QVERIFY(qAbs(resized.height() - original.height()) <= 2);
    if (horizontalEdge <= 0) QVERIFY(qAbs(resized.right() - original.right()) <= 2);
    if (horizontalEdge >= 0) QVERIFY(qAbs(resized.left() - original.left()) <= 2);
    if (verticalEdge <= 0) QVERIFY(qAbs(resized.bottom() - original.bottom()) <= 2);
    if (verticalEdge >= 0) QVERIFY(qAbs(resized.top() - original.top()) <= 2);

    const QString output = qEnvironmentVariable("CARDSTACK_DESIGNER_GALLERY_DIR");
    if (!output.isEmpty()) {
        QDir directory(output);
        QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
        QCoreApplication::processEvents();
        QVERIFY(designer.grab().save(directory.filePath(
            imagePrefix + QStringLiteral("_resize_") + QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".png"))));
    }
    QVERIFY(designer.canUndo());
    designer.undo();
    QCOMPARE(readBounds(), original);
}

} // namespace CardStack::Tests::DesignerResizeProbe
