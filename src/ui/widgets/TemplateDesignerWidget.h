#pragma once

#include "CardTemplateLayout.h"
#include "Deck.h"
#include "FieldDefinition.h"

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QFormLayout;
class QLineEdit;
class QLabel;
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
        QWidget* parent = nullptr,
        DeckAppearance appearance = {},
        QString description = {});
    ~TemplateDesignerWidget() override;

    const CardTemplateLayout& layoutDefinition() const;
    const QVector<FieldDefinition>& fieldDefinitions() const;
    QStringList fieldNames() const;
    const DeckAppearance& appearance() const { return m_appearance; }
    const QString& description() const { return m_description; }
    void setDescription(const QString& description);
    void markSaved();
    bool addDataField(const QString& name, int length, bool phone = false, bool showName = true);
    int selectedFrameIndex() const;
    const QVector<int>& selectedFrameIndices() const { return m_selectedFrameIndices; }
    const CardTemplateFrame* selectedFrameDefinition() const;
    int selectedFieldIndex() const;
    bool isDirty() const;
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canCopyFrame() const;
    bool canPasteFrame() const;
    bool selectionHasStyledFrames() const;
    bool selectionHasStyleFlag(quint8 flag) const;
    int selectionAlignment() const;
    void setSelectionStyleFlag(quint8 flag, bool enabled);
    void setSelectionAlignment(quint8 alignment);
    void markDirty();

signals:
    void commandRequested(int commandId);
    void saveRequested(const CardTemplateLayout& layout);
    void dirtyChanged(bool dirty);
    void selectedFieldChanged();
    void propertiesChanged();

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
    void setAppearance(DeckAppearance appearance);
    void save();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct UndoState {
        CardTemplateLayout layout;
        QVector<FieldDefinition> fields;
        DeckAppearance appearance;
        int selectedFrameIndex = -1;
        QVector<int> selectedFrameIndices;
        int selectionAnchor = -1;
        QString description;
    };

    void pushUndoState();
    void buildUi();
    void refreshFrameTable();
    void refreshFrameProperties();
    void selectFrame(int frameIndex);
    void selectFrames(const QVector<int>& frameIndices);
    void syncFrameTableSelection();
    bool copySelectionToClipboard();
    void refreshFieldChoices();
    void applyPropertyEdits();
    void setPropertyRowVisible(QWidget* field, bool visible);
    CardTemplateFrame defaultFrame(CardTemplateFrameKind kind) const;
    QString frameKindName(CardTemplateFrameKind kind) const;

    CardTemplateLayout m_layout;
    QVector<FieldDefinition> m_fields;
    DeckAppearance m_appearance;
    QString m_description;
    bool m_dirty = false;
    int m_selectedFrameIndex = -1;
    QVector<int> m_selectedFrameIndices;
    QVector<UndoState> m_undoStack;
    bool m_restoringUndoState = false;

    TemplateDesignCanvas* m_canvas = nullptr;
    QTableWidget* m_frameTable = nullptr;
    QLabel* m_selectionSummary = nullptr;
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
