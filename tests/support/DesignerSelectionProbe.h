#pragma once

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMouseEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <algorithm>

namespace CardStack::Tests::DesignerSelectionProbe {

inline void pointer(QWidget* target, QEvent::Type type, QPoint point, Qt::MouseButton button,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, QPointF(point), QPointF(target->mapToGlobal(point)), button, buttons, modifiers);
    QCoreApplication::sendEvent(target, &event);
}

inline void click(QWidget* target, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    pointer(target, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
    pointer(target, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
    QCoreApplication::processEvents();
}

inline QRectF page(QWidget* canvas, QSize modelSize, int rulerThickness)
{
    const QVariant document = canvas->property("designerPageRect");
    if (document.isValid()) return document.toRectF();
    const QRectF available = canvas->rect().adjusted(rulerThickness + 16, rulerThickness + 16, -20, -20);
    const qreal aspect = qreal(modelSize.width()) / modelSize.height();
    qreal width = available.width();
    qreal height = width / aspect;
    if (height > available.height()) { height = available.height(); width = height * aspect; }
    return QRectF(available.center().x() - width / 2, available.center().y() - height / 2, width, height);
}

inline QPoint point(QWidget* canvas, QSize modelSize, int rulerThickness, QPoint modelPoint)
{
    const QRectF bounds = page(canvas, modelSize, rulerThickness);
    return QPoint(qRound(bounds.left() + modelPoint.x() * bounds.width() / modelSize.width()),
                  qRound(bounds.top() + modelPoint.y() * bounds.height() / modelSize.height()));
}

inline void capture(QWidget& designer, const QString& name)
{
    const QString output = qEnvironmentVariable("CARDSTACK_GROUP_GALLERY_DIR");
    if (output.isEmpty()) return;
    QDir directory(output);
    QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
    QCoreApplication::processEvents();
    QVERIFY(designer.grab().save(directory.filePath(name + QStringLiteral(".png"))));
}

template <class Designer>
void selectRows(Designer& designer, const QVector<int>& indices)
{
    auto* table = designer.template findChild<QTableWidget*>();
    QVERIFY(table);
    table->clearSelection();
    for (int index : indices) {
        table->selectionModel()->select(table->model()->index(index, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

template <class Designer>
void expectSelection(const Designer& designer, QVector<int> expected)
{
    QVector<int> actual = designer.selectedFrameIndices();
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    QCOMPARE(actual, expected);
}

template <class Designer, class ReadFrames>
void exercise(Designer& designer, const char* canvasName, QSize modelSize, int rulerThickness,
              int keyboardStep, const QString& prefix, ReadFrames readFrames)
{
    designer.setAttribute(Qt::WA_DontShowOnScreen);
    designer.resize(1080, 760);
    designer.show();
    QCoreApplication::processEvents();
    QWidget* canvas = designer.template findChild<QWidget*>(QString::fromLatin1(canvasName));
    QVERIFY(canvas);
    const auto original = readFrames();
    QCOMPARE(original.size(), 3);
    const auto center = [&](int index) {
        return point(canvas, modelSize, rulerThickness, readFrames().at(index).bounds.center());
    };
    click(canvas, center(0));
    expectSelection(designer, {0});
    click(canvas, center(1), Qt::ControlModifier);
    expectSelection(designer, {0, 1});
    click(canvas, center(0), Qt::ControlModifier);
    expectSelection(designer, {1});
    click(canvas, center(0), Qt::ControlModifier);
    click(canvas, center(1), Qt::ShiftModifier);
    expectSelection(designer, {0, 1, 2});
    QVERIFY(designer.selectedFrameDefinition() == nullptr);
    auto* summary = designer.template findChild<QLabel*>(QStringLiteral("designerSelectionSummary"));
    QVERIFY(summary && !summary->isHidden());
    QVERIFY(summary->text().contains(QStringLiteral("3")));
    QVERIFY(!designer.isDirty());
    QVERIFY(!designer.canUndo());
    capture(designer, prefix + QStringLiteral("_group_selected"));

    QTest::keyClick(canvas, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(readFrames(), original);
    QVERIFY(!designer.canUndo());
    QTest::keyClick(canvas, Qt::Key_Right);
    for (int index = 0; index < original.size(); ++index)
        QCOMPARE(readFrames().at(index).bounds, original.at(index).bounds.translated(keyboardStep, 0));
    designer.undo();
    QCOMPARE(readFrames(), original);
    expectSelection(designer, {0, 1, 2});
    QVERIFY(!designer.canUndo());

    const QPoint start = center(0);
    pointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseMove, start + QPoint(28, 18), Qt::NoButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseButtonRelease, start + QPoint(28, 18), Qt::LeftButton, Qt::NoButton);
    const QPoint translation = readFrames().first().bounds.topLeft() - original.first().bounds.topLeft();
    QVERIFY(!translation.isNull());
    for (int index = 0; index < original.size(); ++index)
        QCOMPARE(readFrames().at(index).bounds, original.at(index).bounds.translated(translation));
    capture(designer, prefix + QStringLiteral("_group_moved"));
    designer.undo();
    QCOMPARE(readFrames(), original);
    expectSelection(designer, {0, 1, 2});
    QVERIFY(!designer.canUndo());

    const QPoint clampStart = center(0);
    pointer(canvas, QEvent::MouseButtonPress, clampStart, Qt::LeftButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseMove, clampStart - QPoint(2000, 2000), Qt::NoButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseButtonRelease, clampStart - QPoint(2000, 2000), Qt::LeftButton, Qt::NoButton);
    const QPoint clamped = readFrames().first().bounds.topLeft() - original.first().bounds.topLeft();
    for (int index = 0; index < original.size(); ++index) {
        QCOMPARE(readFrames().at(index).bounds, original.at(index).bounds.translated(clamped));
        QVERIFY(readFrames().at(index).bounds.left() >= 0);
        QVERIFY(readFrames().at(index).bounds.top() >= 0);
    }
    designer.undo();
    QCOMPARE(readFrames(), original);

    const QPoint marqueeStart = point(canvas, modelSize, rulerThickness, QPoint(700, 500));
    pointer(canvas, QEvent::MouseButtonPress, marqueeStart, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(!summary->isHidden());
    for (auto* spin : designer.template findChildren<QSpinBox*>()) QVERIFY(!spin->isVisibleTo(&designer));
    const QPoint marqueeEnd = point(canvas, modelSize, rulerThickness, QPoint(2500, 1450));
    pointer(canvas, QEvent::MouseMove, marqueeEnd, Qt::NoButton, Qt::LeftButton);
    capture(designer, prefix + QStringLiteral("_marquee_drag"));
    pointer(canvas, QEvent::MouseButtonRelease, marqueeEnd, Qt::LeftButton, Qt::NoButton);
    expectSelection(designer, {0, 2});
    QVERIFY(!designer.canUndo());
    capture(designer, prefix + QStringLiteral("_marquee_intersections"));
    click(canvas, center(1));
    expectSelection(designer, {1});
    QVERIFY(designer.selectedFrameDefinition());
    QVERIFY(summary->isHidden());
    capture(designer, prefix + QStringLiteral("_single_properties_restored"));

    auto* table = designer.template findChild<QTableWidget*>();
    QVERIFY(table);
    table->setCurrentCell(0, 0, QItemSelectionModel::ClearAndSelect);
    QTest::keyClick(table, Qt::Key_Down, Qt::ShiftModifier);
    expectSelection(designer, {0, 1});
    QTest::keyClick(table, Qt::Key_A, Qt::ControlModifier);
    expectSelection(designer, {0, 1, 2});
    QCOMPARE(readFrames(), original);
    QVERIFY(!designer.canUndo());
}

} // namespace CardStack::Tests::DesignerSelectionProbe
