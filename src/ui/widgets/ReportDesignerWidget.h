#pragma once

#include "ReportDefinition.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

namespace CardStack {

class ReportDesignCanvas;

enum class ReportLineBoxShape {
    Box,
    HorizontalLine,
    VerticalLine
};

class ReportDesignerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ReportDesignerWidget(
        ReportDefinition report,
        QStringList fieldNames,
        QWidget* parent = nullptr);

    const ReportDefinition& report() const;
    int selectedFrameIndex() const;
    QStringList fieldNames() const;
    bool isDirty() const;
    void commitPendingEdits();
    void markDirty();
    void setReportName(const QString& name);
    void applyForm(
        ReportFormType formType,
        int formWidth,
        int formHeight,
        int rows,
        int columns,
        int marginLeft = 0,
        int marginTop = 0,
        int marginRight = 0,
        int marginBottom = 0,
        int horizontalGutter = 0,
        int verticalGutter = 0);

signals:
    void commandRequested(int commandId);
    void saveRequested(const ReportDefinition& report);
    void dirtyChanged(bool dirty);

public slots:
    void addTextFrame();
    void addTextFrameWithText(const QString& text, quint8 styleFlags);
    void addDataFrame();
    void addDataFrameForField(const QString& fieldName, quint8 styleFlags, bool printEntireContents);
    void addSystemFrame();
    void addSystemFrameWithText(const QString& text, quint8 styleFlags);
    void addLineBoxFrame();
    void addLineBoxFrameShape(ReportLineBoxShape shape, int lineStyle = 0, int fillPattern = 0, int cornerRadius = 0);
    void deleteSelectedFrame();
    void selectCurrentFrameText();
    void save();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void normalizeReport();
    void buildUi();
    void refreshFrameTable();
    void refreshFrameProperties();
    void selectFrame(int frameIndex);
    void applyPropertyEdits();
    ReportFrameDefinition defaultFrame(ReportFrameKind kind) const;

    ReportDefinition m_report;
    QStringList m_fieldNames;
    bool m_dirty = false;
    int m_selectedFrameIndex = -1;

    ReportDesignCanvas* m_canvas = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QTableWidget* m_frameTable = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QComboBox* m_kindCombo = nullptr;
    QComboBox* m_alignmentCombo = nullptr;
    QComboBox* m_lineShapeCombo = nullptr;
    QComboBox* m_lineStyleCombo = nullptr;
    QComboBox* m_fillPatternCombo = nullptr;
    QCheckBox* m_printEntireContents = nullptr;
    QCheckBox* m_boldCheck = nullptr;
    QCheckBox* m_italicCheck = nullptr;
    QCheckBox* m_underlineCheck = nullptr;
    QSpinBox* m_cornerRadiusSpin = nullptr;
    QSpinBox* m_leftSpin = nullptr;
    QSpinBox* m_topSpin = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
};

} // namespace CardStack
