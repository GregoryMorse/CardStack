#include "ReportDesignerWidget.h"

#include "../support/ModalDialogDriver.h"

#include <QMessageBox>
#include <QSignalSpy>
#include <QTest>

using namespace CardStack;

namespace {

ReportDefinition makeReport()
{
    ReportDefinition report;
    report.name = QStringLiteral("Editable Report");
    report.formWidth = 5000;
    report.formHeight = 3000;
    report.formType = ReportFormType::Report;
    return report;
}

} // namespace

class ReportDesignerWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void addsSelectsAndSavesFrames()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

        designer.addDataFrame();
        QVERIFY(designer.isDirty());
        QCOMPARE(designer.report().frames.size(), 1);
        QCOMPARE(designer.selectedFrameIndex(), 0);
        QCOMPARE(designer.report().frames.first().kind, ReportFrameKind::Data);
        QCOMPARE(designer.report().frames.first().fieldPlaceholders, QVector<QString>({QStringLiteral("Product")}));

        designer.addLineBoxFrame();
        QCOMPARE(designer.report().frames.size(), 2);
        QCOMPARE(designer.selectedFrameIndex(), 1);

        designer.deleteSelectedFrame();
        QCOMPARE(designer.report().frames.size(), 1);
        QCOMPARE(designer.selectedFrameIndex(), 0);

        designer.save();
        QCOMPARE(saveSpy.size(), 1);
        QVERIFY(!designer.isDirty());
    }

    void appliesReportMargins()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});

        designer.applyForm(
            ReportFormType::Report,
            8500,
            11000,
            1,
            1,
            500,
            500,
            500,
            500);

        const ReportDefinition report = designer.report();
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.marginLeft, 500);
        QCOMPARE(report.marginTop, 500);
        QCOMPARE(report.marginRight, 500);
        QCOMPARE(report.marginBottom, 500);
        QVERIFY(designer.isDirty());
    }

    void closePromptCancelKeepsDesignerOpen()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
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
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Discarded"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(designer.close());
        QVERIFY(!designer.isVisible());
        QCOMPARE(saveSpy.size(), 0);
    }

    void closePromptSaveEmitsSaveAndCloses()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

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
int runReportDesignerWidgetTests(int argc, char** argv)
{
    ReportDesignerWidgetTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(ReportDesignerWidgetTests)
#endif

#include "ReportDesignerWidgetTests.moc"

