#include "TemplateDesignerWidget.h"
#include <QApplication>

#include "../support/KeyboardSelectionProbe.h"
#include "../support/ModalDialogDriver.h"
#include "UiIds.h"
#include "../support/DesignerResizeProbe.h"
#include "../support/DesignerGridProbe.h"
#include "../support/DesignerSelectionProbe.h"
#include "DesignerClipboard.h"
#include "ReportDesignerWidget.h"
#include "TemplateDesignerWidget.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

using namespace CardStack;

namespace {

QVector<FieldDefinition> makeFields()
{
    return {
        FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64),
        FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 8192),
    };
}

bool saveWidgetImage(QWidget& widget, const QDir& outputDirectory, const QString& fileName)
{
    widget.resize(980, 720);
    widget.show();
    if (!QTest::qWaitForWindowExposed(&widget)) {
        return false;
    }
    QCoreApplication::processEvents();
    QTest::qWait(30);
    const QPixmap pixmap = widget.grab();
    return !pixmap.isNull() && pixmap.save(outputDirectory.filePath(fileName));
}

} // namespace

class TemplateDesignerWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void selectionCallbacksStopBeforeDesignerTeardown()
    {
        Tests::KeyboardSelectionProbe::designerTeardown(new TemplateDesignerWidget(Tests::KeyboardSelectionProbe::layout(), {}), QStringLiteral("templateDesignCanvas"));
    }
    void frameTableDoesNotCallOwnerForLifecycleEvents() { Tests::KeyboardSelectionProbe::frameTableIgnoresLifecycleEvents(); }
    void modalFrameKeyboardSelection_data()
    {
        QTest::addColumn<bool>("tableMode");
        QTest::newRow("canvas") << false; QTest::newRow("frame-list") << true;
    }
    void modalFrameKeyboardSelection()
    {
        QFETCH(bool, tableMode);
        TemplateDesignerWidget designer(Tests::KeyboardSelectionProbe::layout(), {});
        Tests::KeyboardSelectionProbe::designerModes(designer, tableMode, QStringLiteral("templateDesignCanvas"));
    }
    void writesSelectionModeInspectionImages()
    {
        TemplateDesignerWidget designer(Tests::KeyboardSelectionProbe::layout(), {});
        Tests::KeyboardSelectionProbe::designerGallery(designer, QStringLiteral("templateDesignCanvas"), QStringLiteral("template"));
    }

    void sourceGridMatchesOriginalIntegerAlgorithm() { CardStack::Tests::DesignerGridProbe::sourceAlgorithm(); }
    void keyboardGridDoesNotAccumulateRoundingError() { CardStack::Tests::DesignerGridProbe::templateKeyboard(); }
    void pointerGridIsIndependentOfViewportZoom() { CardStack::Tests::DesignerGridProbe::templatePointer(); }
    void clipboardUsesOriginalNormalizedOriginWithoutStagger() { CardStack::Tests::DesignerGridProbe::templateClipboard(); }
    void textFontsScaleWithTemplateGeometry() { CardStack::Tests::DesignerGridProbe::templateFonts(); }
    void missingFontsPreserveTemplateGeometry() { CardStack::Tests::DesignerGridProbe::templateFonts(QStringLiteral("CardStack Missing Font Test")); }
    void tableMouseSelectionRestoresKindSpecificProperties() { CardStack::Tests::DesignerGridProbe::templateTable(); }

    void addsIndependentDataFieldsAndPreservesAppearance()
    {
        TemplateDesignerWidget designer({}, {FieldDefinition(QStringLiteral("Original"), FieldType::Text, 40)});
        QVERIFY(designer.addDataField(QStringLiteral("Artist"), 80, false, true));
        QVERIFY(designer.addDataField(QStringLiteral("Album"), 120, false, true));
        QVERIFY(!designer.addDataField(QStringLiteral("artist"), 80));
        QCOMPARE(designer.fieldDefinitions().size(), 3);
        QCOMPARE(designer.layoutDefinition().frames.at(0).fieldIndex, 1);
        QCOMPARE(designer.layoutDefinition().frames.at(1).fieldIndex, 2);
        DeckAppearance appearance;
        appearance.dataFont = QApplication::font().toString();
        appearance.useSystemColors = false;
        designer.setAppearance(appearance);
        QCOMPARE(designer.appearance(), appearance);
        designer.undo();
        QCOMPARE(designer.fieldDefinitions().size(), 3);
        QVERIFY(designer.appearance() != appearance);
        designer.undo();
        QCOMPARE(designer.fieldDefinitions().size(), 2);
    }
    void preservesExactImportedFrameBounds()
    {
        CardTemplateLayout importedLayout;
        importedLayout.canvasWidth = 6400;
        importedLayout.canvasHeight = 4800;
        importedLayout.frames = {
            {CardTemplateFrameKind::Text, QRect(117, 203, 941, 211), QStringLiteral("Title"), -1},
            {CardTemplateFrameKind::DataBox, QRect(44, 612, 1777, 321), {}, 0},
            {CardTemplateFrameKind::NotesBox, QRect(2010, 999, 2444, 1555), {}, 1},
            {CardTemplateFrameKind::LineOrBox, QRect(300, 3700, 5100, 48), {}, -1},
        };

        TemplateDesignerWidget designer(importedLayout, makeFields());

        QCOMPARE(designer.layoutDefinition().canvasWidth, importedLayout.canvasWidth);
        QCOMPARE(designer.layoutDefinition().canvasHeight, importedLayout.canvasHeight);
        QCOMPARE(designer.layoutDefinition().frames.size(), importedLayout.frames.size());
        for (int index = 0; index < importedLayout.frames.size(); ++index) {
            QCOMPARE(designer.layoutDefinition().frames.at(index).bounds, importedLayout.frames.at(index).bounds);
            QCOMPARE(designer.layoutDefinition().frames.at(index).kind, importedLayout.frames.at(index).kind);
        }
    }

    void addsAndSavesTemplateFrames()
    {
        CardTemplateLayout initialLayout;
        TemplateDesignerWidget designer(initialLayout, makeFields());
        QSignalSpy dirtySpy(&designer, &TemplateDesignerWidget::dirtyChanged);
        QSignalSpy saveSpy(&designer, &TemplateDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Heading"), 1);
        designer.addDataBoxFrameForField(QStringLiteral("Name"), 0);
        designer.addNotesBoxFrame();
        designer.addLineBoxFrameShape(CardTemplateLineBoxShape::HorizontalLine, 2, 3, 4);

        QCOMPARE(designer.layoutDefinition().frames.size(), 4);
        QCOMPARE(designer.layoutDefinition().frames.at(0).kind, CardTemplateFrameKind::Text);
        QCOMPARE(designer.layoutDefinition().frames.at(0).text, QStringLiteral("Heading"));
        QCOMPARE(designer.layoutDefinition().frames.at(1).kind, CardTemplateFrameKind::DataBox);
        QCOMPARE(designer.layoutDefinition().frames.at(1).fieldIndex, 0);
        QCOMPARE(designer.layoutDefinition().frames.at(2).kind, CardTemplateFrameKind::NotesBox);
        QCOMPARE(designer.layoutDefinition().frames.at(2).fieldIndex, 1);
        QCOMPARE(designer.layoutDefinition().frames.at(3).kind, CardTemplateFrameKind::LineOrBox);
        QCOMPARE(designer.layoutDefinition().frames.at(3).lineBoxShape, CardTemplateLineBoxShape::HorizontalLine);
        QCOMPARE(designer.layoutDefinition().frames.at(3).lineStyle, 2);
        QCOMPARE(designer.layoutDefinition().frames.at(3).fillPattern, 3);
        QCOMPARE(designer.layoutDefinition().frames.at(3).cornerRadius, 4);
        QVERIFY(designer.isDirty());
        QVERIFY(!dirtySpy.isEmpty());

        designer.save();
        QCOMPARE(saveSpy.size(), 1);
        QVERIFY(!designer.isDirty());
    }

    void undoRestoresTemplateFieldsAndFramesTogether()
    {
        CardTemplateLayout initialLayout;
        TemplateDesignerWidget designer(initialLayout, makeFields());
        QVERIFY(!designer.canUndo());

        designer.addDataBoxFrameForField(QStringLiteral("Name"), 0);
        QVERIFY(designer.canUndo());
        QCOMPARE(designer.layoutDefinition().frames.size(), 1);
        QCOMPARE(designer.fieldDefinitions().first().name(), QStringLiteral("Name"));

        designer.updateSelectedFieldDefinition(QStringLiteral("Display Name"), 80, true, true);
        QCOMPARE(designer.fieldDefinitions().first().name(), QStringLiteral("Display Name"));
        QCOMPARE(designer.layoutDefinition().frames.first().text, QStringLiteral("Display Name"));

        designer.undo();
        QCOMPARE(designer.fieldDefinitions().first().name(), QStringLiteral("Name"));
        QCOMPARE(designer.layoutDefinition().frames.first().text, QStringLiteral("Name"));

        designer.undo();
        QVERIFY(designer.layoutDefinition().frames.isEmpty());
        QCOMPARE(designer.selectedFrameIndex(), -1);
        QVERIFY(!designer.canUndo());
    }

    void addingNotesBoxCreatesBackingNotesFieldWhenMissing()
    {
        CardTemplateLayout initialLayout;
        QVector<FieldDefinition> fields = {
            FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255),
            FieldDefinition(QStringLiteral("Notes"), FieldType::Text, 255),
        };
        TemplateDesignerWidget designer(initialLayout, fields);

        designer.addNotesBoxFrame();

        QCOMPARE(designer.selectedFrameDefinition()->kind, CardTemplateFrameKind::NotesBox);
        QVERIFY(designer.selectedFieldIndex() >= 0);
        const FieldDefinition& notesField = designer.fieldDefinitions().at(designer.selectedFieldIndex());
        QCOMPARE(notesField.name(), QStringLiteral("Notes 2"));
        QCOMPARE(notesField.type(), FieldType::Notes);
        QCOMPARE(notesField.maxLength(), 8192);
        QCOMPARE(designer.selectedFrameDefinition()->fieldIndex, designer.selectedFieldIndex());
        QCOMPARE(designer.selectedFrameDefinition()->text, notesField.name());
    }

    void closePromptCancelKeepsDesignerOpen()
    {
        CardTemplateLayout initialLayout;
        TemplateDesignerWidget designer(initialLayout, makeFields());
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));

        designer.addTextFrameWithText(QStringLiteral("Unsaved"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Cancel);
        QVERIFY(!designer.close());
        QVERIFY(designer.isVisible());
        QVERIFY(designer.isDirty());
    }

    void closePromptDiscardClosesWithoutSaving()
    {
        CardTemplateLayout initialLayout;
        TemplateDesignerWidget designer(initialLayout, makeFields());
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &TemplateDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Discarded"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(designer.close());
        QVERIFY(!designer.isVisible());
        QCOMPARE(saveSpy.size(), 0);
    }

    void closePromptSaveEmitsSaveAndCloses()
    {
        CardTemplateLayout initialLayout;
        TemplateDesignerWidget designer(initialLayout, makeFields());
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &TemplateDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Saved"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Save);
        QVERIFY(designer.close());
        QVERIFY(!designer.isVisible());
        QCOMPARE(saveSpy.size(), 1);
        QVERIFY(!designer.isDirty());
    }

    void propertyRowsFollowSelectedFrameKindAndLineShapeEditsPersist()
    {
        CardTemplateLayout layout;
        layout.frames = {
            {CardTemplateFrameKind::Text, QRect(300, 280, 1800, 260), QStringLiteral("Template heading"), -1},
            {CardTemplateFrameKind::DataBox, QRect(300, 740, 2200, 320), {}, 0},
            {CardTemplateFrameKind::LineOrBox, QRect(300, 2400, 3600, 700), {}, -1},
        };

        TemplateDesignerWidget designer(layout, makeFields());

        auto* frameTable = designer.findChild<QTableWidget*>();
        auto* textEdit = designer.findChild<QLineEdit*>(QStringLiteral("templateTextEdit"));
        auto* fieldCombo = designer.findChild<QComboBox*>(QStringLiteral("templateFieldCombo"));
        auto* lineShapeCombo = designer.findChild<QComboBox*>(QStringLiteral("templateLineShapeCombo"));
        auto* lineStyleSpin = designer.findChild<QSpinBox*>(QStringLiteral("templateLineStyleSpin"));
        QVERIFY(frameTable != nullptr);
        QVERIFY(textEdit != nullptr);
        QVERIFY(fieldCombo != nullptr);
        QVERIFY(lineShapeCombo != nullptr);
        QVERIFY(lineStyleSpin != nullptr);

        Tests::DesignerSelectionProbe::selectRows(designer, {0});
        QCoreApplication::processEvents();
        QVERIFY(!textEdit->isHidden());
        QVERIFY(fieldCombo->isHidden());
        QVERIFY(lineShapeCombo->isHidden());

        Tests::DesignerSelectionProbe::selectRows(designer, {1});
        QCoreApplication::processEvents();
        QVERIFY(textEdit->isHidden());
        QVERIFY(!fieldCombo->isHidden());
        QVERIFY(lineShapeCombo->isHidden());

        Tests::DesignerSelectionProbe::selectRows(designer, {2});
        QCoreApplication::processEvents();
        QVERIFY(textEdit->isHidden());
        QVERIFY(fieldCombo->isHidden());
        QVERIFY(!lineShapeCombo->isHidden());
        QVERIFY(!lineStyleSpin->isHidden());

        lineShapeCombo->setCurrentIndex(lineShapeCombo->findData(static_cast<int>(CardTemplateLineBoxShape::HorizontalLine)));
        QCoreApplication::processEvents();
        QCOMPARE(designer.layoutDefinition().frames.at(2).lineBoxShape, CardTemplateLineBoxShape::HorizontalLine);

        lineShapeCombo->setCurrentIndex(lineShapeCombo->findData(static_cast<int>(CardTemplateLineBoxShape::VerticalLine)));
        QCoreApplication::processEvents();
        QCOMPARE(designer.layoutDefinition().frames.at(2).lineBoxShape, CardTemplateLineBoxShape::VerticalLine);
    }

    void templateGroupClipboardRemapsFieldsAcrossWindowsAndUndo()
    {
        if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
            QSKIP("Clipboard probes require the isolated offscreen clipboard.");
        const QVector<FieldDefinition> fields = {
            FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64, true, false, QByteArray("name-metadata"), 180),
            FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 8192),
        };
        CardTemplateLayout layout;
        layout.frames = {
            {CardTemplateFrameKind::DataBox, QRect(700, 600, 1800, 320), QStringLiteral("Name"), 0},
            {CardTemplateFrameKind::DataBox, QRect(700, 1100, 1800, 320), QStringLiteral("Name"), 0},
            {CardTemplateFrameKind::NotesBox, QRect(700, 1600, 2400, 800), QStringLiteral("Notes"), 1},
            {CardTemplateFrameKind::Text, QRect(700, 300, 1800, 220), QStringLiteral("Name"), -1},
        };
        layout.frames[0].styleFlags = CardTemplateStyleFlagUnderline | CardTemplateStyleFlagAlignRight;
        layout.frames[0].legacyDescriptor = QByteArray("frame-metadata");
        TemplateDesignerWidget source(layout, fields);
        source.show();
        QApplication::setActiveWindow(&source);
        Tests::DesignerSelectionProbe::selectRows(source, {0, 1, 2, 3});
        source.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"))->setFocus();
        source.copySelectedFrame();
        const auto packet = DesignerClipboard::readTemplate();
        const QPoint copyShift = packet.frames.first().bounds.topLeft() - source.layoutDefinition().frames.first().bounds.topLeft();
        for (int index = 0; index < packet.frames.size(); ++index) {
            auto expected = source.layoutDefinition().frames.at(index);
            expected.bounds.translate(copyShift);
            QCOMPARE(packet.frames.at(index), expected);
        }
        QCOMPARE(packet.fields.size(), 2);
        QCOMPARE(packet.fields.value(0).legacyDescriptor(), fields.at(0).legacyDescriptor());
        QCOMPARE(packet.fields.value(0).displayWidth(), 180);

        TemplateDesignerWidget target({}, {FieldDefinition(QStringLiteral("Name"), FieldType::Text, 20), fields.at(1)});
        target.show();
        QApplication::setActiveWindow(&target);
        target.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"))->setFocus();
        QVERIFY(target.canPasteFrame());
        target.pasteFrame();
        QCOMPARE(target.layoutDefinition().frames.size(), 4);
        QCOMPARE(target.fieldDefinitions().size(), 3);
        QCOMPARE(target.layoutDefinition().frames.at(0).fieldIndex, 2);
        QCOMPARE(target.layoutDefinition().frames.at(1).fieldIndex, 2);
        QCOMPARE(target.layoutDefinition().frames.at(2).fieldIndex, 1);
        QCOMPARE(target.fieldDefinitions().at(2).maxLength(), 64);
        QCOMPARE(target.fieldDefinitions().at(2).displayWidth(), 180);
        QCOMPARE(target.fieldDefinitions().at(2).legacyDescriptor(), fields.at(0).legacyDescriptor());
        QVERIFY(target.fieldDefinitions().at(2).name() != QStringLiteral("Name"));
        QCOMPARE(target.layoutDefinition().frames.at(3).text, target.fieldDefinitions().at(2).name());
        QCOMPARE(target.layoutDefinition().frames.at(0).styleFlags, layout.frames.at(0).styleFlags);
        QCOMPARE(target.layoutDefinition().frames.at(0).legacyDescriptor, layout.frames.at(0).legacyDescriptor);
        const QPoint shift = target.layoutDefinition().frames.at(0).bounds.topLeft() - layout.frames.at(0).bounds.topLeft();
        for (int index = 0; index < layout.frames.size(); ++index)
            QCOMPARE(target.layoutDefinition().frames.at(index).bounds, layout.frames.at(index).bounds.translated(shift));
        target.undo();
        QVERIFY(target.layoutDefinition().frames.isEmpty());
        QCOMPARE(target.fieldDefinitions().size(), 2);
        auto* choices = target.findChild<QComboBox*>(QStringLiteral("templateFieldCombo"));
        QVERIFY(choices);
        QCOMPARE(choices->findData(2), -1);

        QApplication::setActiveWindow(&source);
        source.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"))->setFocus();
        source.cutSelectedFrame();
        QVERIFY(source.layoutDefinition().frames.isEmpty());
        QCOMPARE(source.fieldDefinitions().size(), 2);
        source.undo();
        QCOMPARE(source.layoutDefinition().frames, layout.frames);
        Tests::DesignerSelectionProbe::expectSelection(source, {0, 1, 2, 3});
        source.pasteFrame();
        QCOMPARE(source.layoutDefinition().frames.size(), 8);
        QCOMPARE(source.fieldDefinitions().size(), 2);
        source.undo();
        QCOMPARE(source.layoutDefinition().frames, layout.frames);
        ReportDesignerWidget otherFormat({}, {});
        QVERIFY(!otherFormat.canPasteFrame());
    }

    void groupFormattingChangesOnlyApplicableTemplateItems()
    {
        CardTemplateLayout layout;
        layout.frames = {
            {CardTemplateFrameKind::Text, QRect(500, 500, 1800, 300), QStringLiteral("Caption"), -1},
            {CardTemplateFrameKind::DataBox, QRect(500, 1000, 1800, 300), QStringLiteral("Name"), 0},
            {CardTemplateFrameKind::LineOrBox, QRect(500, 1500, 1800, 300), {}, -1},
        };
        layout.frames[0].styleFlags = CardTemplateStyleFlagBold;
        layout.frames[1].styleFlags = CardTemplateStyleFlagItalic | CardTemplateStyleFlagAlignCenter;
        TemplateDesignerWidget designer(layout, makeFields());
        Tests::DesignerSelectionProbe::selectRows(designer, {0, 1, 2});
        const auto before = designer.layoutDefinition().frames;
        QVERIFY(!designer.selectionHasStyleFlag(CardTemplateStyleFlagBold));
        QCOMPARE(designer.selectionAlignment(), -1);
        designer.setSelectionStyleFlag(CardTemplateStyleFlagBold, true);
        QVERIFY(designer.selectionHasStyleFlag(CardTemplateStyleFlagBold));
        QCOMPARE(designer.layoutDefinition().frames.at(2), before.at(2));
        designer.undo();
        QCOMPARE(designer.layoutDefinition().frames, before);
        Tests::DesignerSelectionProbe::expectSelection(designer, {0, 1, 2});
        designer.setSelectionAlignment(CardTemplateStyleFlagAlignRight);
        QCOMPARE(designer.selectionAlignment(), int(CardTemplateStyleFlagAlignRight));
        QVERIFY((designer.layoutDefinition().frames.at(1).styleFlags & CardTemplateStyleFlagItalic) != 0);
        QCOMPARE(designer.layoutDefinition().frames.at(2), before.at(2));
        designer.undo();
        QCOMPARE(designer.layoutDefinition().frames, before);
    }

    void focusedTextClipboardDoesNotCopyOrDeleteTemplateObjects()
    {
        if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
            QSKIP("Clipboard probes require the isolated offscreen clipboard.");
        CardTemplateLayout layout;
        layout.frames = {{CardTemplateFrameKind::Text, QRect(500, 500, 2000, 320), QStringLiteral("Caption editing"), -1}};
        TemplateDesignerWidget designer(layout, makeFields());
        designer.show();
        QApplication::setActiveWindow(&designer);
        auto* edit = designer.findChild<QLineEdit*>(QStringLiteral("templateTextEdit"));
        QVERIFY(edit);
        edit->setFocus();
        edit->selectAll();
        QCoreApplication::processEvents();
        designer.copySelectedFrame();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Caption editing"));
        QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(DesignerClipboard::TemplateMime)));
        designer.cutSelectedFrame();
        QVERIFY(edit->text().isEmpty());
        QCOMPARE(designer.layoutDefinition().frames.size(), 1);
        QApplication::clipboard()->setText(QStringLiteral("New caption"));
        designer.pasteFrame();
        QCOMPARE(edit->text(), QStringLiteral("New caption"));
        edit->selectAll();
        designer.deleteSelectedFrame();
        QVERIFY(edit->text().isEmpty());
        QCOMPARE(designer.layoutDefinition().frames.size(), 1);
    }

    void selectionMessagesPreserveOriginalGroupBehavior()
    {
        CardTemplateLayout layout;
        layout.canvasWidth = 6400;
        layout.canvasHeight = 4800;
        for (const QRect& bounds : {QRect(900, 700, 1200, 400), QRect(3000, 2100, 1200, 400), QRect(2200, 1300, 800, 400)})
            layout.frames.append({CardTemplateFrameKind::Text, bounds, QStringLiteral("Object %1").arg(layout.frames.size() + 1), -1});
        TemplateDesignerWidget designer(layout, makeFields());
        Tests::DesignerSelectionProbe::exercise(designer, "templateDesignCanvas",
            QSize(layout.canvasWidth, layout.canvasHeight), 0, 30,
            QStringLiteral("template"), [&designer]() { return designer.layoutDefinition().frames; });
    }

    void resizesAllEdgesAndCorners_data()
    {
        Tests::DesignerResizeProbe::addData();
    }

    void resizesAllEdgesAndCorners()
    {
        CardTemplateLayout layout;
        layout.canvasWidth = 6400;
        layout.canvasHeight = 4800;
        layout.frames = {
            {CardTemplateFrameKind::Text, QRect(900, 700, 1600, 500), QStringLiteral("Resize from any side"), -1},
        };
        TemplateDesignerWidget designer(layout, makeFields());
        Tests::DesignerResizeProbe::exercise(designer, "templateDesignCanvas",
            QSize(layout.canvasWidth, layout.canvasHeight), layout.frames.first().bounds, 0, QStringLiteral("template"),
            [&designer]() { return designer.layoutDefinition().frames.first().bounds; });
    }

    void canvasMouseMessagesAndClipboardPreserveDesignerBehavior()
    {
        CardTemplateLayout layout;
        layout.canvasWidth = 6400;
        layout.canvasHeight = 4800;
        layout.frames = {
            {CardTemplateFrameKind::Text, QRect(900, 700, 1600, 500), QStringLiteral("Move me"), -1},
        };

        TemplateDesignerWidget designer(layout, makeFields());
        designer.resize(1000, 700);
        designer.show();
        QCoreApplication::processEvents();
        QWidget* canvas = designer.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"));
        QVERIFY(canvas != nullptr);

        const QRectF available = canvas->rect().adjusted(16, 16, -20, -20);
        const qreal aspect = static_cast<qreal>(layout.canvasWidth) / layout.canvasHeight;
        qreal pageWidth = available.width();
        qreal pageHeight = pageWidth / aspect;
        if (pageHeight > available.height()) {
            pageHeight = available.height();
            pageWidth = pageHeight * aspect;
        }
        const QRectF page(available.center().x() - pageWidth / 2.0,
                          available.center().y() - pageHeight / 2.0,
                          pageWidth,
                          pageHeight);
        const auto visualBounds = [&page, &layout](const QRect& bounds) {
            return QRectF(page.left() + bounds.left() * page.width() / layout.canvasWidth,
                          page.top() + bounds.top() * page.height() / layout.canvasHeight,
                          bounds.width() * page.width() / layout.canvasWidth,
                          bounds.height() * page.height() / layout.canvasHeight);
        };

        const QPoint initialCenter = visualBounds(layout.frames.first().bounds).center().toPoint();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter);
        QCOMPARE(designer.selectedFrameIndex(), 0);
        QVERIFY(canvas->hasFocus());

        const QRect beforeMove = designer.layoutDefinition().frames.first().bounds;
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter);
        QTest::mouseMove(canvas, initialCenter + QPoint(24, 18));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter + QPoint(24, 18));
        const QRect afterMove = designer.layoutDefinition().frames.first().bounds;
        QVERIFY(afterMove.topLeft() != beforeMove.topLeft());

        const QPoint resizePoint = visualBounds(afterMove).bottomRight().toPoint() - QPoint(2, 2);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, resizePoint);
        QTest::mouseMove(canvas, resizePoint + QPoint(22, 16));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, resizePoint + QPoint(22, 16));
        const QRect afterResize = designer.layoutDefinition().frames.first().bounds;
        QVERIFY(afterResize.width() > afterMove.width());
        QVERIFY(afterResize.height() > afterMove.height());

        designer.copySelectedFrame();
        QVERIFY(designer.canPasteFrame());
        designer.pasteFrame();
        QCOMPARE(designer.layoutDefinition().frames.size(), 2);
        QCOMPARE(designer.selectedFrameIndex(), 1);
        QVERIFY(designer.layoutDefinition().frames.last().bounds.topLeft() != afterResize.topLeft());
        designer.undo();
        QCOMPARE(designer.layoutDefinition().frames.size(), 1);
        QCOMPARE(designer.layoutDefinition().frames.first().bounds, afterResize);

        QSignalSpy commandSpy(&designer, &TemplateDesignerWidget::commandRequested);
        QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, visualBounds(afterResize).center().toPoint());
        QCOMPARE(commandSpy.size(), 1);
        QCOMPARE(commandSpy.takeFirst().first().toInt(), UiIds::Command::ToolFrameAttributes);
    }

    void writesManualDesignerInspectionImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_DESIGNER_GALLERY_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_DESIGNER_GALLERY_DIR to write template designer PNGs for manual inspection.");
        }

        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        CardTemplateLayout layout;
        layout.canvasWidth = 6400;
        layout.canvasHeight = 4800;
        layout.frames = {
            {CardTemplateFrameKind::Text, QRect(300, 280, 1800, 260), QStringLiteral("Template heading"), -1},
            {CardTemplateFrameKind::DataBox, QRect(300, 740, 2200, 320), {}, 0},
            {CardTemplateFrameKind::NotesBox, QRect(300, 1260, 3200, 900), {}, 1},
            {CardTemplateFrameKind::LineOrBox, QRect(300, 2400, 3600, 70), {}, -1},
        };
        layout.frames.last().lineBoxShape = CardTemplateLineBoxShape::Box;

        TemplateDesignerWidget designer(layout, makeFields());
        QVERIFY(saveWidgetImage(designer, outputDirectory, QStringLiteral("template_designer_all_elements.png")));

        QTableWidget* frameTable = designer.findChild<QTableWidget*>();
        QVERIFY(frameTable != nullptr);
        const QVector<QString> stateNames = {
            QStringLiteral("text_selected"),
            QStringLiteral("data_box_selected"),
            QStringLiteral("notes_box_selected"),
            QStringLiteral("line_box_selected"),
        };
        for (int row = 0; row < stateNames.size(); ++row) {
            frameTable->setCurrentCell(row, 0);
            frameTable->selectRow(row);
            QCoreApplication::processEvents();
            QTest::qWait(30);
            QVERIFY(saveWidgetImage(
                designer,
                outputDirectory,
                QStringLiteral("template_designer_%1.png").arg(stateNames.at(row))));
        }
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runTemplateDesignerWidgetTests(int argc, char** argv)
{
    TemplateDesignerWidgetTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(TemplateDesignerWidgetTests)
#endif

#include "TemplateDesignerWidgetTests.moc"

