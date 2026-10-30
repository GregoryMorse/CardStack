#pragma once

#include "ReportDefinition.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QFormLayout;
class QLineEdit;
class QLabel;
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
    ~ReportDesignerWidget() override;

    const ReportDefinition& report() const;
    int selectedFrameIndex() const;
    const QVector<int>& selectedFrameIndices() const { return m_selectedFrameIndices; }
    const ReportFrameDefinition* selectedFrameDefinition() const;
    QStringList fieldNames() const;
    bool isDirty() const;
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canCopyFrame() const;
    bool canPasteFrame() const;
    bool selectionHasStyledFrames() const;
    bool selectionHasStyleFlag(quint8 flag) const;
    int selectionAlignment() const;
    void setSelectionStyleFlag(quint8 flag, bool enabled);
    void setSelectionAlignment(quint8 alignment);
    void commitPendingEdits();
    void markDirty();
    void setReportName(const QString& name);
    void setReportIdentity(const QString& id) { m_report.documentId = id; }
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
        int verticalGutter = 0,
        int headerHeight = 0,
        int footerHeight = 0,
        int pageWidth = 0, int pageHeight = 0, int orientation = -1, int paperStyleId = 0);

signals:
    void commandRequested(int commandId);
    void saveRequested(const ReportDefinition& report);
    void dirtyChanged(bool dirty);
    void selectedFrameChanged();
    void propertiesChanged();

public slots:
    void undo();
    void addTextFrame();
    void addTextFrameWithText(const QString& text, quint8 styleFlags);
    void addDataFrame();
    void addDataFrameForField(const QString& fieldName, quint8 styleFlags, bool printEntireContents);
    void addSystemFrame();
    void addSystemFrameWithText(const QString& text, quint8 styleFlags);
    void addLineBoxFrame();
    void addLineBoxFrameShape(ReportLineBoxShape shape, int lineStyle = 0, int fillPattern = 0, int cornerRadius = 0);
    void deleteSelectedFrame();
    void cutSelectedFrame();
    void copySelectedFrame();
    void pasteFrame();
    void selectCurrentFrameText();
    void updateSelectedFrameFromToolbar(
        const QString& text,
        quint8 styleFlags,
        bool printEntireContents,
        int lineBoxShape,
        int lineStyle,
        int fillPattern,
        int cornerRadius);
    void setReportFont(bool dataFont, ReportFontDefinition font);
    void save();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct UndoState {
        ReportDefinition report;
        int selectedFrameIndex = -1;
        QVector<int> selectedFrameIndices;
        int selectionAnchor = -1;
    };

    void pushUndoState();
    void normalizeReport();
    void buildUi();
    void refreshFrameTable();
    void refreshFrameProperties();
    void selectFrame(int frameIndex);
    void selectFrames(const QVector<int>& frameIndices);
    void syncFrameTableSelection();
    bool copySelectionToClipboard();
    void applyPropertyEdits();
    void setPropertyRowVisible(QWidget* field, bool visible);
    ReportFrameDefinition defaultFrame(ReportFrameKind kind) const;

    ReportDefinition m_report;
    QStringList m_fieldNames;
    bool m_dirty = false;
    int m_selectedFrameIndex = -1;
    QVector<int> m_selectedFrameIndices;
    QVector<UndoState> m_undoStack;
    bool m_restoringUndoState = false;

    ReportDesignCanvas* m_canvas = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QTableWidget* m_frameTable = nullptr;
    QLabel* m_selectionSummary = nullptr;
    QFormLayout* m_propertyForm = nullptr;
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
