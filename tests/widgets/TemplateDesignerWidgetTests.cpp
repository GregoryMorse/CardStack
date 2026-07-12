#include "TemplateDesignerWidget.h"

#include "../support/ModalDialogDriver.h"

#include <QMessageBox>
#include <QSignalSpy>
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

} // namespace

class TemplateDesignerWidgetTests : public QObject {
    Q_OBJECT

private slots:
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

