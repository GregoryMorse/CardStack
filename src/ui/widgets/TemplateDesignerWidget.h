#pragma once

#include "CardTemplateLayout.h"
#include "FieldDefinition.h"

#include <QStringList>
#include <QWidget>
#include <optional>

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QFormLayout;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace CardStack {

class TemplateDesignCanvas;

class TemplateDesignerWidget : public QWidget {
    Q_OBJECT

public:
    explicit TemplateDesignerWidget(
        CardTemplateLayout layout,
        QVector<FieldDefinition> fields,
        QWidget* parent = nullptr);

    const CardTemplateLayout& layoutDefinition() const;
    const QVector<FieldDefinition>& fieldDefinitions() const;
    QStringList fieldNames() const;
    int selectedFrameIndex() const;
    const CardTemplateFrame* selectedFrameDefinition() const;
    int selectedFieldIndex() const;
    bool isDirty() const;
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canCopyFrame() const { return m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size(); }
    bool canPasteFrame() const { return m_copiedFrame.has_value(); }
    void markDirty();

signals:
    void saveRequested(const CardTemplateLayout& layout);
    void dirtyChanged(bool dirty);
    void selectedFieldChanged();

public slots:
    void undo();
    void addTextFrame();
    void addTextFrameWithText(const QString& text, quint8 styleFlags = 0);
    void addDataBoxFrame();
    void addDataBoxFrameForField(const QString& fieldName, quint8 styleFlags = 0);
    void addNotesBoxFrame();
    void addLineBoxFrame();
    void addLineBoxFrameShape(CardTemplateLineBoxShape shape, int lineStyle, int fillPattern, int cornerRadius);
    void deleteSelectedFrame();
    void cutSelectedFrame();
    void copySelectedFrame();
    void pasteFrame();
    void updateSelectedFieldDefinition(const QString& name, int maxLength, bool phone, bool showName);
    void updateSelectedFrameFromToolbar(
        const QString& text,
        quint8 styleFlags,
        CardTemplateLineBoxShape lineBoxShape,
        int lineStyle,
        int fillPattern,
        int cornerRadius);
    void save();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct UndoState {
        CardTemplateLayout layout;
        QVector<FieldDefinition> fields;
        int selectedFrameIndex = -1;
    };

    void pushUndoState();
    void buildUi();
    void refreshFrameTable();
    void refreshFrameProperties();
    void selectFrame(int frameIndex);
    void applyPropertyEdits();
    void setPropertyRowVisible(QWidget* field, bool visible);
    CardTemplateFrame defaultFrame(CardTemplateFrameKind kind) const;
    QString frameKindName(CardTemplateFrameKind kind) const;

    CardTemplateLayout m_layout;
    QVector<FieldDefinition> m_fields;
    bool m_dirty = false;
    int m_selectedFrameIndex = -1;
    std::optional<CardTemplateFrame> m_copiedFrame;
    QVector<UndoState> m_undoStack;
    bool m_restoringUndoState = false;

    TemplateDesignCanvas* m_canvas = nullptr;
    QTableWidget* m_frameTable = nullptr;
    QFormLayout* m_propertyForm = nullptr;
    QComboBox* m_kindCombo = nullptr;
    QComboBox* m_fieldCombo = nullptr;
    QComboBox* m_lineShapeCombo = nullptr;
    QLineEdit* m_textEdit = nullptr;
    QCheckBox* m_boldCheck = nullptr;
    QCheckBox* m_italicCheck = nullptr;
    QCheckBox* m_underlineCheck = nullptr;
    QSpinBox* m_leftSpin = nullptr;
    QSpinBox* m_topSpin = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QSpinBox* m_lineStyleSpin = nullptr;
    QSpinBox* m_fillPatternSpin = nullptr;
    QSpinBox* m_cornerRadiusSpin = nullptr;
};

} // namespace CardStack
