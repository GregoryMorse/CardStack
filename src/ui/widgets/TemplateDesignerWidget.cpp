#include "TemplateDesignerWidget.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace CardStack {
namespace {

QRectF scaledBounds(const QRect& bounds, const CardTemplateLayout& layout, const QRectF& page)
{
    return QRectF(
        page.left() + bounds.left() * page.width() / std::max(1, layout.canvasWidth),
        page.top() + bounds.top() * page.height() / std::max(1, layout.canvasHeight),
        bounds.width() * page.width() / std::max(1, layout.canvasWidth),
        bounds.height() * page.height() / std::max(1, layout.canvasHeight));
}

int lineBoxShapeToInt(CardTemplateLineBoxShape shape)
{
    return static_cast<int>(shape);
}

CardTemplateLineBoxShape lineBoxShapeFromInt(int value)
{
    switch (value) {
    case static_cast<int>(CardTemplateLineBoxShape::HorizontalLine):
        return CardTemplateLineBoxShape::HorizontalLine;
    case static_cast<int>(CardTemplateLineBoxShape::VerticalLine):
        return CardTemplateLineBoxShape::VerticalLine;
    case static_cast<int>(CardTemplateLineBoxShape::Box):
    default:
        return CardTemplateLineBoxShape::Box;
    }
}

QRect unscaledBounds(const QRectF& frameRect, const CardTemplateLayout& layout, const QRectF& page)
{
    return QRect(
        qRound((frameRect.left() - page.left()) * std::max(1, layout.canvasWidth) / page.width()),
        qRound((frameRect.top() - page.top()) * std::max(1, layout.canvasHeight) / page.height()),
        qRound(frameRect.width() * std::max(1, layout.canvasWidth) / page.width()),
        qRound(frameRect.height() * std::max(1, layout.canvasHeight) / page.height()));
}

QRect normalizedFrameBounds(QRect bounds, const CardTemplateLayout& layout)
{
    bounds.setWidth(std::max(1, bounds.width()));
    bounds.setHeight(std::max(1, bounds.height()));
    bounds.moveLeft(std::clamp(bounds.left(), 0, std::max(0, layout.canvasWidth - bounds.width())));
    bounds.moveTop(std::clamp(bounds.top(), 0, std::max(0, layout.canvasHeight - bounds.height())));
    return bounds;
}

} // namespace

class TemplateDesignCanvas : public QWidget {
public:
    explicit TemplateDesignCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("templateDesignCanvas"));
        setMinimumSize(420, 360);
        setMouseTracking(true);
    }

    void setLayoutDefinition(const CardTemplateLayout* layout)
    {
        m_layout = layout;
        update();
    }

    void setFieldDefinitions(const QVector<FieldDefinition>* fields)
    {
        m_fields = fields;
        update();
    }

    void setSelectedFrameIndex(int frameIndex)
    {
        m_selectedFrameIndex = frameIndex;
        update();
    }

    std::function<void(int)> frameSelected;
    std::function<void(int, QRect)> frameBoundsChanged;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().window());
        if (m_layout == nullptr) {
            return;
        }

        const QRectF page = pageRect();
        painter.fillRect(page.translated(5.0, 5.0), QColor(0, 0, 0, 35));
        painter.fillRect(page, palette().base());
        painter.setPen(palette().mid().color());
        painter.drawRect(page.adjusted(0, 0, -1, -1));

        const QRect indexHeader(0, 0, m_layout->canvasWidth, 220);
        painter.fillRect(indexHeader, palette().alternateBase());
        painter.setPen(QPen(palette().mid().color(), 12));
        painter.drawRect(indexHeader.adjusted(6, 6, -6, -6));
        QFont indexFont = painter.font();
        indexFont.setPixelSize(30);
        indexFont.setWeight(QFont::Medium);
        painter.setFont(indexFont);
        painter.setPen(palette().text().color());
        painter.drawText(indexHeader.adjusted(30, 0, -30, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("Index"));

        for (int index = 0; index < m_layout->frames.size(); ++index) {
            const CardTemplateFrame& frame = m_layout->frames.at(index);
            const QRectF frameRect = scaledBounds(frame.bounds, *m_layout, page);
            painter.setPen(index == m_selectedFrameIndex ? palette().highlight().color() : QColor(50, 105, 145));

            switch (frame.kind) {
            case CardTemplateFrameKind::LineOrBox:
                if (frame.lineBoxShape == CardTemplateLineBoxShape::HorizontalLine) {
                    painter.drawLine(frameRect.left(), frameRect.center().y(), frameRect.right(), frameRect.center().y());
                } else if (frame.lineBoxShape == CardTemplateLineBoxShape::VerticalLine) {
                    painter.drawLine(frameRect.center().x(), frameRect.top(), frameRect.center().x(), frameRect.bottom());
                } else {
                    painter.drawRoundedRect(frameRect, frame.cornerRadius, frame.cornerRadius);
                }
                break;
            case CardTemplateFrameKind::DataBox:
            case CardTemplateFrameKind::NotesBox:
                painter.drawRect(frameRect);
                painter.drawText(frameRect.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignVCenter, tr("Sample data"));
                if (m_fields != nullptr
                    && frame.fieldIndex >= 0
                    && frame.fieldIndex < m_fields->size()
                    && m_fields->at(frame.fieldIndex).showName()
                    && !std::any_of(
                        m_layout->frames.cbegin(),
                        m_layout->frames.cend(),
                        [&frame](const CardTemplateFrame& candidate) {
                            if (candidate.kind != CardTemplateFrameKind::Text) {
                                return false;
                            }
                            const bool matchingText = candidate.text.compare(frame.text, Qt::CaseInsensitive) == 0;
                            const bool adjacentCaption = candidate.bounds.left() < frame.bounds.right()
                                && candidate.bounds.right() > frame.bounds.left()
                                && candidate.bounds.bottom() <= frame.bounds.top()
                                && frame.bounds.top() - candidate.bounds.bottom() <= 300;
                            return matchingText || adjacentCaption;
                        })) {
                    const QRectF labelRect(page.left(), frameRect.top(), std::max<qreal>(0.0, frameRect.left() - page.left() - 6.0), frameRect.height());
                    painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, m_fields->at(frame.fieldIndex).name());
                }
                break;
            case CardTemplateFrameKind::Text:
                painter.drawText(
                    frameRect,
                    ((frame.styleFlags & CardTemplateStyleFlagAlignRight) != 0
                            ? Qt::AlignRight
                            : ((frame.styleFlags & CardTemplateStyleFlagAlignCenter) != 0 ? Qt::AlignHCenter : Qt::AlignLeft))
                        | Qt::AlignVCenter,
                    frame.text);
                painter.drawRect(frameRect.adjusted(0, 0, -1, -1));
                break;
            }

            if (index == m_selectedFrameIndex) {
                painter.drawRect(frameRect.adjusted(2, 2, -3, -3));
                painter.setBrush(palette().highlight());
                painter.setPen(Qt::NoPen);
                painter.drawRect(resizeHandle(frameRect));
                painter.setBrush(Qt::NoBrush);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (m_layout == nullptr) {
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_layout->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page);
            if (frameRect.contains(event->position())) {
                if (frameSelected) {
                    frameSelected(index);
                }
                m_dragFrameIndex = index;
                m_dragStartPosition = event->position();
                m_dragStartBounds = m_layout->frames.at(index).bounds;
                m_dragMode = resizeHandle(frameRect).contains(event->position()) ? DragMode::Resize : DragMode::Move;
                return;
            }
        }
        if (frameSelected) {
            frameSelected(-1);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_layout == nullptr) {
            return;
        }

        if (m_dragMode != DragMode::None && m_dragFrameIndex >= 0) {
            const QRectF page = pageRect();
            const QPointF delta = event->position() - m_dragStartPosition;
            QRectF visual = scaledBounds(m_dragStartBounds, *m_layout, page);
            if (m_dragMode == DragMode::Move) {
                visual.translate(delta);
            } else {
                visual.setWidth(std::max<qreal>(8.0, visual.width() + delta.x()));
                visual.setHeight(std::max<qreal>(8.0, visual.height() + delta.y()));
            }

            QRect bounds = normalizedFrameBounds(unscaledBounds(visual, *m_layout, page), *m_layout);
            if (frameBoundsChanged) {
                frameBoundsChanged(m_dragFrameIndex, bounds);
            }
            return;
        }

        updateCursor(event->position());
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        m_dragMode = DragMode::None;
        m_dragFrameIndex = -1;
        updateCursor(mapFromGlobal(QCursor::pos()));
    }

private:
    enum class DragMode {
        None,
        Move,
        Resize
    };

    QRectF pageRect() const
    {
        const qreal aspect = m_layout == nullptr ? 4.0 / 3.0 : static_cast<qreal>(m_layout->canvasWidth) / std::max(1, m_layout->canvasHeight);
        QRectF available = rect().adjusted(16, 16, -20, -20);
        qreal pageWidth = available.width();
        qreal pageHeight = pageWidth / aspect;
        if (pageHeight > available.height()) {
            pageHeight = available.height();
            pageWidth = pageHeight * aspect;
        }
        return QRectF(available.center().x() - pageWidth / 2.0, available.center().y() - pageHeight / 2.0, pageWidth, pageHeight);
    }

    QRectF resizeHandle(const QRectF& frameRect) const
    {
        return QRectF(frameRect.right() - 7.0, frameRect.bottom() - 7.0, 7.0, 7.0);
    }

    void updateCursor(const QPointF& position)
    {
        if (m_layout == nullptr) {
            unsetCursor();
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_layout->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page);
            if (resizeHandle(frameRect).contains(position)) {
                setCursor(Qt::SizeFDiagCursor);
                return;
            }
            if (frameRect.contains(position)) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
        unsetCursor();
    }

    const CardTemplateLayout* m_layout = nullptr;
    const QVector<FieldDefinition>* m_fields = nullptr;
    int m_selectedFrameIndex = -1;
    int m_dragFrameIndex = -1;
    DragMode m_dragMode = DragMode::None;
    QPointF m_dragStartPosition;
    QRect m_dragStartBounds;
};

TemplateDesignerWidget::TemplateDesignerWidget(CardTemplateLayout layout, QVector<FieldDefinition> fields, QWidget* parent)
    : QWidget(parent)
    , m_layout(std::move(layout))
    , m_fields(std::move(fields))
{
    for (CardTemplateFrame& frame : m_layout.frames) {
        const bool fieldFrame = frame.kind == CardTemplateFrameKind::DataBox
            || frame.kind == CardTemplateFrameKind::NotesBox;
        if (fieldFrame
            && frame.text.trimmed().isEmpty()
            && frame.fieldIndex >= 0
            && frame.fieldIndex < m_fields.size()) {
            frame.text = m_fields.at(frame.fieldIndex).name();
        }
    }
    buildUi();
    refreshFrameTable();
    selectFrame(m_layout.frames.isEmpty() ? -1 : 0);
}

const CardTemplateLayout& TemplateDesignerWidget::layoutDefinition() const
{
    return m_layout;
}

const QVector<FieldDefinition>& TemplateDesignerWidget::fieldDefinitions() const
{
    return m_fields;
}

QStringList TemplateDesignerWidget::fieldNames() const
{
    QStringList names;
    names.reserve(m_fields.size());
    for (const FieldDefinition& field : m_fields) {
        names.append(field.name());
    }
    return names;
}

int TemplateDesignerWidget::selectedFrameIndex() const
{
    return m_selectedFrameIndex;
}

const CardTemplateFrame* TemplateDesignerWidget::selectedFrameDefinition() const
{
    return m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size()
        ? &m_layout.frames.at(m_selectedFrameIndex)
        : nullptr;
}

int TemplateDesignerWidget::selectedFieldIndex() const
{
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
        return -1;
    }
    const CardTemplateFrame& frame = m_layout.frames.at(m_selectedFrameIndex);
    if (frame.kind != CardTemplateFrameKind::DataBox && frame.kind != CardTemplateFrameKind::NotesBox) {
        return -1;
    }
    return frame.fieldIndex;
}

bool TemplateDesignerWidget::isDirty() const
{
    return m_dirty;
}

void TemplateDesignerWidget::addTextFrame()
{
    addTextFrameWithText(tr("Template Text"), 0);
}

void TemplateDesignerWidget::addTextFrameWithText(const QString& text, quint8 styleFlags)
{
    pushUndoState();
    CardTemplateFrame frame = defaultFrame(CardTemplateFrameKind::Text);
    if (!text.isEmpty()) {
        frame.text = text;
    }
    frame.styleFlags = styleFlags;
    m_layout.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_layout.frames.size() - 1);
    markDirty();
}

void TemplateDesignerWidget::addDataBoxFrame()
{
    addDataBoxFrameForField({}, 0);
}

void TemplateDesignerWidget::addDataBoxFrameForField(const QString& fieldName, quint8 styleFlags)
{
    pushUndoState();
    CardTemplateFrame frame = defaultFrame(CardTemplateFrameKind::DataBox);
    const QString trimmedFieldName = fieldName.trimmed();
    if (!trimmedFieldName.isEmpty()) {
        for (int index = 0; index < m_fields.size(); ++index) {
            if (m_fields.at(index).name().compare(trimmedFieldName, Qt::CaseInsensitive) == 0) {
                frame.fieldIndex = index;
                frame.text = m_fields.at(index).name();
                break;
            }
        }
    }
    frame.styleFlags = styleFlags;
    m_layout.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_layout.frames.size() - 1);
    markDirty();
}

void TemplateDesignerWidget::addNotesBoxFrame()
{
    pushUndoState();
    CardTemplateFrame frame = defaultFrame(CardTemplateFrameKind::NotesBox);
    if (frame.fieldIndex < 0) {
        const QString baseName = tr("Notes");
        QString fieldName = baseName;
        int suffix = 2;
        const auto fieldNameExists = [this](const QString& candidate) {
            return std::any_of(m_fields.cbegin(), m_fields.cend(), [&candidate](const FieldDefinition& field) {
                return field.name().compare(candidate, Qt::CaseInsensitive) == 0;
            });
        };
        while (fieldNameExists(fieldName)) {
            fieldName = tr("%1 %2").arg(baseName).arg(suffix++);
        }
        m_fields.append(FieldDefinition(fieldName, FieldType::Notes, 8192));
        frame.fieldIndex = m_fields.size() - 1;
        frame.text = fieldName;
        if (m_fieldCombo != nullptr) {
            m_fieldCombo->addItem(fieldName);
        }
    }
    m_layout.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_layout.frames.size() - 1);
    markDirty();
}

void TemplateDesignerWidget::addLineBoxFrame()
{
    addLineBoxFrameShape(CardTemplateLineBoxShape::Box, 0, 0, 0);
}

void TemplateDesignerWidget::addLineBoxFrameShape(CardTemplateLineBoxShape shape, int lineStyle, int fillPattern, int cornerRadius)
{
    pushUndoState();
    CardTemplateFrame frame = defaultFrame(CardTemplateFrameKind::LineOrBox);
    frame.lineBoxShape = shape;
    frame.lineStyle = lineStyle;
    frame.fillPattern = fillPattern;
    frame.cornerRadius = cornerRadius;
    m_layout.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_layout.frames.size() - 1);
    markDirty();
}

void TemplateDesignerWidget::deleteSelectedFrame()
{
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
        return;
    }
    pushUndoState();
    m_layout.frames.removeAt(m_selectedFrameIndex);
    refreshFrameTable();
    selectFrame(std::min(m_selectedFrameIndex, static_cast<int>(m_layout.frames.size()) - 1));
    markDirty();
}

void TemplateDesignerWidget::copySelectedFrame()
{
    if (m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size()) {
        m_copiedFrame = m_layout.frames.at(m_selectedFrameIndex);
    }
}

void TemplateDesignerWidget::cutSelectedFrame()
{
    copySelectedFrame();
    if (m_copiedFrame.has_value()) {
        deleteSelectedFrame();
    }
}

void TemplateDesignerWidget::pasteFrame()
{
    if (!m_copiedFrame.has_value()) {
        return;
    }
    pushUndoState();
    CardTemplateFrame frame = *m_copiedFrame;
    frame.bounds.translate(120, 120);
    m_layout.frames.append(std::move(frame));
    refreshFrameTable();
    selectFrame(m_layout.frames.size() - 1);
    markDirty();
}

void TemplateDesignerWidget::updateSelectedFieldDefinition(
    const QString& name,
    int maxLength,
    bool phone,
    bool showName)
{
    const int fieldIndex = selectedFieldIndex();
    if (fieldIndex < 0 || fieldIndex >= m_fields.size()) {
        return;
    }

    const FieldDefinition previous = m_fields.at(fieldIndex);
    const QString normalizedName = name.trimmed().isEmpty() ? previous.name() : name.trimmed();
    const FieldDefinition updated(
        normalizedName,
        previous.type(),
        std::max(1, maxLength),
        showName,
        phone,
        previous.legacyDescriptor());
    if (previous.name() == updated.name()
        && previous.maxLength() == updated.maxLength()
        && previous.showName() == updated.showName()
        && previous.isPhone() == updated.isPhone()) {
        return;
    }

    pushUndoState();
    m_fields[fieldIndex] = updated;
    for (CardTemplateFrame& frame : m_layout.frames) {
        if ((frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox)
            && frame.fieldIndex == fieldIndex) {
            frame.text = updated.name();
        } else if (frame.kind == CardTemplateFrameKind::Text
            && frame.text.compare(previous.name(), Qt::CaseInsensitive) == 0) {
            frame.text = updated.name();
        }
    }
    const int comboIndex = m_fieldCombo->findData(fieldIndex);
    if (comboIndex >= 0) {
        m_fieldCombo->setItemText(comboIndex, updated.name());
    }
    refreshFrameTable();
    markDirty();
}

void TemplateDesignerWidget::updateSelectedFrameFromToolbar(
    const QString& text,
    quint8 styleFlags,
    CardTemplateLineBoxShape lineBoxShape,
    int lineStyle,
    int fillPattern,
    int cornerRadius)
{
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
        return;
    }
    pushUndoState();
    CardTemplateFrame& frame = m_layout.frames[m_selectedFrameIndex];
    if (frame.kind == CardTemplateFrameKind::Text) {
        frame.text = text;
        frame.styleFlags = styleFlags;
    } else if (frame.kind == CardTemplateFrameKind::LineOrBox) {
        frame.lineBoxShape = lineBoxShape;
        frame.lineStyle = std::max(0, lineStyle);
        frame.fillPattern = std::max(0, fillPattern);
        frame.cornerRadius = std::max(0, cornerRadius);
    }
    refreshFrameTable();
    refreshFrameProperties();
    markDirty();
}

void TemplateDesignerWidget::save()
{
    applyPropertyEdits();
    m_dirty = false;
    emit dirtyChanged(false);
    emit saveRequested(m_layout);
}

void TemplateDesignerWidget::closeEvent(QCloseEvent* event)
{
    if (!m_dirty) {
        event->accept();
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("CardStack Templates"),
        tr("Save changes to the card layout before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (choice == QMessageBox::Save) {
        save();
        if (m_dirty) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

void TemplateDesignerWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* splitter = new QSplitter(this);
    m_canvas = new TemplateDesignCanvas(splitter);
    m_canvas->setLayoutDefinition(&m_layout);
    m_canvas->setFieldDefinitions(&m_fields);
    splitter->addWidget(m_canvas);

    auto* side = new QWidget(splitter);
    auto* sideLayout = new QVBoxLayout(side);
    m_frameTable = new QTableWidget(side);
    m_frameTable->setColumnCount(3);
    m_frameTable->setHorizontalHeaderLabels({tr("#"), tr("Kind"), tr("Text")});
    m_frameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setSelectionMode(QAbstractItemView::SingleSelection);
    sideLayout->addWidget(m_frameTable, 1);

    auto* form = new QFormLayout;
    m_propertyForm = form;
    m_kindCombo = new QComboBox(side);
    m_kindCombo->setObjectName(QStringLiteral("templateKindCombo"));
    m_kindCombo->addItem(frameKindName(CardTemplateFrameKind::Text), static_cast<int>(CardTemplateFrameKind::Text));
    m_kindCombo->addItem(frameKindName(CardTemplateFrameKind::DataBox), static_cast<int>(CardTemplateFrameKind::DataBox));
    m_kindCombo->addItem(frameKindName(CardTemplateFrameKind::NotesBox), static_cast<int>(CardTemplateFrameKind::NotesBox));
    m_kindCombo->addItem(frameKindName(CardTemplateFrameKind::LineOrBox), static_cast<int>(CardTemplateFrameKind::LineOrBox));
    m_fieldCombo = new QComboBox(side);
    m_fieldCombo->setObjectName(QStringLiteral("templateFieldCombo"));
    m_fieldCombo->addItem(tr("(none)"), -1);
    for (int index = 0; index < m_fields.size(); ++index) {
        m_fieldCombo->addItem(m_fields.at(index).name(), index);
    }
    m_lineShapeCombo = new QComboBox(side);
    m_lineShapeCombo->setObjectName(QStringLiteral("templateLineShapeCombo"));
    m_lineShapeCombo->addItem(tr("Box"), lineBoxShapeToInt(CardTemplateLineBoxShape::Box));
    m_lineShapeCombo->addItem(tr("Horizontal line"), lineBoxShapeToInt(CardTemplateLineBoxShape::HorizontalLine));
    m_lineShapeCombo->addItem(tr("Vertical line"), lineBoxShapeToInt(CardTemplateLineBoxShape::VerticalLine));
    m_textEdit = new QLineEdit(side);
    m_textEdit->setObjectName(QStringLiteral("templateTextEdit"));
    m_boldCheck = new QCheckBox(tr("Bold"), side);
    m_boldCheck->setObjectName(QStringLiteral("templateBoldCheck"));
    m_italicCheck = new QCheckBox(tr("Italic"), side);
    m_italicCheck->setObjectName(QStringLiteral("templateItalicCheck"));
    m_underlineCheck = new QCheckBox(tr("Underline"), side);
    m_underlineCheck->setObjectName(QStringLiteral("templateUnderlineCheck"));
    m_leftSpin = new QSpinBox(side);
    m_leftSpin->setObjectName(QStringLiteral("templateLeftSpin"));
    m_topSpin = new QSpinBox(side);
    m_topSpin->setObjectName(QStringLiteral("templateTopSpin"));
    m_widthSpin = new QSpinBox(side);
    m_widthSpin->setObjectName(QStringLiteral("templateWidthSpin"));
    m_heightSpin = new QSpinBox(side);
    m_heightSpin->setObjectName(QStringLiteral("templateHeightSpin"));
    m_lineStyleSpin = new QSpinBox(side);
    m_lineStyleSpin->setObjectName(QStringLiteral("templateLineStyleSpin"));
    m_fillPatternSpin = new QSpinBox(side);
    m_fillPatternSpin->setObjectName(QStringLiteral("templateFillPatternSpin"));
    m_cornerRadiusSpin = new QSpinBox(side);
    m_cornerRadiusSpin->setObjectName(QStringLiteral("templateCornerRadiusSpin"));
    for (QSpinBox* spin : {m_leftSpin, m_topSpin, m_widthSpin, m_heightSpin}) {
        spin->setRange(0, 20000);
    }
    for (QSpinBox* spin : {m_lineStyleSpin, m_fillPatternSpin, m_cornerRadiusSpin}) {
        spin->setRange(0, 255);
    }
    m_widthSpin->setMinimum(1);
    m_heightSpin->setMinimum(1);
    form->addRow(tr("Kind"), m_kindCombo);
    form->addRow(tr("Text"), m_textEdit);
    form->addRow(tr("Data box"), m_fieldCombo);
    form->addRow(tr("Line shape"), m_lineShapeCombo);
    form->addRow(tr("Style"), m_boldCheck);
    form->addRow(QString(), m_italicCheck);
    form->addRow(QString(), m_underlineCheck);
    form->addRow(tr("Left"), m_leftSpin);
    form->addRow(tr("Top"), m_topSpin);
    form->addRow(tr("Width"), m_widthSpin);
    form->addRow(tr("Height"), m_heightSpin);
    form->addRow(tr("Line style"), m_lineStyleSpin);
    form->addRow(tr("Fill pattern"), m_fillPatternSpin);
    form->addRow(tr("Corner radius"), m_cornerRadiusSpin);
    sideLayout->addLayout(form);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(m_frameTable, &QTableWidget::currentCellChanged, this, [this](int row) { selectFrame(row); });
    m_canvas->frameSelected = [this](int frameIndex) { selectFrame(frameIndex); };
    m_canvas->frameBoundsChanged = [this](int frameIndex, const QRect& bounds) {
        if (frameIndex < 0 || frameIndex >= m_layout.frames.size()) {
            return;
        }
        m_layout.frames[frameIndex].bounds = bounds;
        if (m_selectedFrameIndex != frameIndex) {
            selectFrame(frameIndex);
        } else {
            refreshFrameProperties();
        }
        refreshFrameTable();
        markDirty();
    };

    const auto propertyChanged = [this] {
        applyPropertyEdits();
        refreshFrameTable();
        refreshFrameProperties();
        markDirty();
    };
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_fieldCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_lineShapeCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_textEdit, &QLineEdit::textEdited, this, propertyChanged);
    connect(m_boldCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_italicCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_underlineCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_leftSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_topSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_widthSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_heightSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_lineStyleSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_fillPatternSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_cornerRadiusSpin, &QSpinBox::valueChanged, this, propertyChanged);
}

void TemplateDesignerWidget::refreshFrameTable()
{
    const QSignalBlocker blocker(m_frameTable);
    m_frameTable->setRowCount(m_layout.frames.size());
    for (int row = 0; row < m_layout.frames.size(); ++row) {
        const CardTemplateFrame& frame = m_layout.frames.at(row);
        m_frameTable->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_frameTable->setItem(row, 1, new QTableWidgetItem(frameKindName(frame.kind)));
        QString displayText = frame.text;
        if (displayText.trimmed().isEmpty()
            && frame.fieldIndex >= 0
            && frame.fieldIndex < m_fields.size()) {
            displayText = m_fields.at(frame.fieldIndex).name();
        }
        if (displayText.trimmed().isEmpty() && frame.kind == CardTemplateFrameKind::DataBox) {
            displayText = tr("Data field");
        } else if (displayText.trimmed().isEmpty() && frame.kind == CardTemplateFrameKind::NotesBox) {
            displayText = tr("Notes field");
        }
        m_frameTable->setItem(row, 2, new QTableWidgetItem(displayText));
    }
    if (m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_frameTable->rowCount()) {
        m_frameTable->setCurrentCell(m_selectedFrameIndex, 0);
    }
    if (m_canvas != nullptr) {
        m_canvas->update();
    }
}

void TemplateDesignerWidget::refreshFrameProperties()
{
    const bool hasFrame = m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size();
    for (QWidget* widget : {static_cast<QWidget*>(m_kindCombo), static_cast<QWidget*>(m_fieldCombo),
             static_cast<QWidget*>(m_lineShapeCombo), static_cast<QWidget*>(m_textEdit),
             static_cast<QWidget*>(m_boldCheck), static_cast<QWidget*>(m_italicCheck),
             static_cast<QWidget*>(m_underlineCheck), static_cast<QWidget*>(m_leftSpin),
             static_cast<QWidget*>(m_topSpin), static_cast<QWidget*>(m_widthSpin),
             static_cast<QWidget*>(m_heightSpin), static_cast<QWidget*>(m_lineStyleSpin),
             static_cast<QWidget*>(m_fillPatternSpin), static_cast<QWidget*>(m_cornerRadiusSpin)}) {
        widget->setEnabled(hasFrame);
    }
    if (!hasFrame) {
        return;
    }

    const CardTemplateFrame& frame = m_layout.frames.at(m_selectedFrameIndex);
    const QSignalBlocker kindBlocker(m_kindCombo);
    const QSignalBlocker fieldBlocker(m_fieldCombo);
    const QSignalBlocker shapeBlocker(m_lineShapeCombo);
    const QSignalBlocker textBlocker(m_textEdit);
    const QSignalBlocker boldBlocker(m_boldCheck);
    const QSignalBlocker italicBlocker(m_italicCheck);
    const QSignalBlocker underlineBlocker(m_underlineCheck);
    const QSignalBlocker leftBlocker(m_leftSpin);
    const QSignalBlocker topBlocker(m_topSpin);
    const QSignalBlocker widthBlocker(m_widthSpin);
    const QSignalBlocker heightBlocker(m_heightSpin);
    const QSignalBlocker lineStyleBlocker(m_lineStyleSpin);
    const QSignalBlocker fillPatternBlocker(m_fillPatternSpin);
    const QSignalBlocker radiusBlocker(m_cornerRadiusSpin);

    m_kindCombo->setCurrentIndex(std::max(0, m_kindCombo->findData(static_cast<int>(frame.kind))));
    m_fieldCombo->setCurrentIndex(std::max(0, m_fieldCombo->findData(frame.fieldIndex)));
    m_lineShapeCombo->setCurrentIndex(std::max(0, m_lineShapeCombo->findData(lineBoxShapeToInt(frame.lineBoxShape))));
    m_textEdit->setText(frame.text);
    m_boldCheck->setChecked((frame.styleFlags & CardTemplateStyleFlagBold) != 0);
    m_italicCheck->setChecked((frame.styleFlags & CardTemplateStyleFlagItalic) != 0);
    m_underlineCheck->setChecked((frame.styleFlags & CardTemplateStyleFlagUnderline) != 0);
    m_leftSpin->setValue(frame.bounds.left());
    m_topSpin->setValue(frame.bounds.top());
    m_widthSpin->setValue(std::max(1, frame.bounds.width()));
    m_heightSpin->setValue(std::max(1, frame.bounds.height()));
    m_lineStyleSpin->setValue(frame.lineStyle);
    m_fillPatternSpin->setValue(frame.fillPattern);
    m_cornerRadiusSpin->setValue(frame.cornerRadius);

    const bool lineOrBox = frame.kind == CardTemplateFrameKind::LineOrBox;
    const bool textFrame = frame.kind == CardTemplateFrameKind::Text;
    const bool dataFrame = frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox;
    const bool styledTextFrame = textFrame || dataFrame;
    setPropertyRowVisible(m_textEdit, textFrame);
    setPropertyRowVisible(m_fieldCombo, dataFrame);
    setPropertyRowVisible(m_boldCheck, styledTextFrame);
    setPropertyRowVisible(m_italicCheck, styledTextFrame);
    setPropertyRowVisible(m_underlineCheck, styledTextFrame);
    setPropertyRowVisible(m_lineShapeCombo, lineOrBox);
    setPropertyRowVisible(m_lineStyleSpin, lineOrBox);
    setPropertyRowVisible(m_fillPatternSpin, lineOrBox);
    setPropertyRowVisible(m_cornerRadiusSpin, lineOrBox);
    m_lineShapeCombo->setEnabled(lineOrBox);
    m_lineStyleSpin->setEnabled(lineOrBox);
    m_fillPatternSpin->setEnabled(lineOrBox);
    m_cornerRadiusSpin->setEnabled(lineOrBox);
}

void TemplateDesignerWidget::selectFrame(int frameIndex)
{
    const int normalizedFrameIndex = frameIndex >= 0 && frameIndex < m_layout.frames.size() ? frameIndex : -1;
    if (normalizedFrameIndex != m_selectedFrameIndex) {
        applyPropertyEdits();
    }
    m_selectedFrameIndex = normalizedFrameIndex;
    if (m_canvas != nullptr) {
        m_canvas->setSelectedFrameIndex(m_selectedFrameIndex);
    }
    refreshFrameProperties();
    emit selectedFieldChanged();
}

void TemplateDesignerWidget::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged(true);
    }
    if (m_canvas != nullptr) {
        m_canvas->update();
    }
}

void TemplateDesignerWidget::pushUndoState()
{
    if (m_restoringUndoState) {
        return;
    }
    constexpr int MaximumUndoStates = 100;
    if (m_undoStack.size() >= MaximumUndoStates) {
        m_undoStack.removeFirst();
    }
    m_undoStack.append({m_layout, m_fields, m_selectedFrameIndex});
}

void TemplateDesignerWidget::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_restoringUndoState = true;
    const UndoState state = m_undoStack.takeLast();
    m_layout = state.layout;
    m_fields = state.fields;
    if (m_canvas != nullptr) {
        m_canvas->setLayoutDefinition(&m_layout);
        m_canvas->setFieldDefinitions(&m_fields);
    }
    refreshFrameTable();
    selectFrame(std::clamp(state.selectedFrameIndex, -1, static_cast<int>(m_layout.frames.size()) - 1));
    m_restoringUndoState = false;
    markDirty();
}

void TemplateDesignerWidget::applyPropertyEdits()
{
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
        return;
    }
    const CardTemplateFrame previousFrame = m_layout.frames.at(m_selectedFrameIndex);
    pushUndoState();
    CardTemplateFrame& frame = m_layout.frames[m_selectedFrameIndex];
    frame.kind = static_cast<CardTemplateFrameKind>(m_kindCombo->currentData().toInt());
    frame.fieldIndex = m_fieldCombo->currentData().toInt();
    if ((frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox)
        && frame.fieldIndex >= 0
        && frame.fieldIndex < m_fields.size()) {
        frame.text = m_fields.at(frame.fieldIndex).name();
    } else if (frame.kind == CardTemplateFrameKind::Text) {
        frame.text = m_textEdit->text();
    } else {
        frame.text.clear();
    }
    frame.bounds = QRect(m_leftSpin->value(), m_topSpin->value(), m_widthSpin->value(), m_heightSpin->value());
    frame.lineBoxShape = lineBoxShapeFromInt(m_lineShapeCombo->currentData().toInt());
    frame.lineStyle = m_lineStyleSpin->value();
    frame.fillPattern = m_fillPatternSpin->value();
    frame.cornerRadius = m_cornerRadiusSpin->value();
    quint8 styleFlags = frame.styleFlags & (CardTemplateStyleFlagAlignCenter | CardTemplateStyleFlagAlignRight);
    if (m_boldCheck->isChecked()) {
        styleFlags |= CardTemplateStyleFlagBold;
    }
    if (m_italicCheck->isChecked()) {
        styleFlags |= CardTemplateStyleFlagItalic;
    }
    if (m_underlineCheck->isChecked()) {
        styleFlags |= CardTemplateStyleFlagUnderline;
    }
    frame.styleFlags = styleFlags;
    if (frame == previousFrame && !m_undoStack.isEmpty()) {
        m_undoStack.removeLast();
    }
}

void TemplateDesignerWidget::setPropertyRowVisible(QWidget* field, bool visible)
{
    if (field == nullptr) {
        return;
    }

    field->setVisible(visible);
    if (m_propertyForm == nullptr) {
        return;
    }
    if (QWidget* label = m_propertyForm->labelForField(field)) {
        label->setVisible(visible);
    }
}

CardTemplateFrame TemplateDesignerWidget::defaultFrame(CardTemplateFrameKind kind) const
{
    CardTemplateFrame frame;
    frame.kind = kind;
    const int frameIndex = m_layout.frames.size();
    const int left = 1400 + (frameIndex % 3) * 180;
    const int top = 500 + frameIndex * 320;
    frame.bounds = QRect(left, top, 1800, kind == CardTemplateFrameKind::NotesBox ? 900 : 260);
    switch (kind) {
    case CardTemplateFrameKind::Text:
        frame.text = tr("Template Text");
        break;
    case CardTemplateFrameKind::DataBox:
        frame.fieldIndex = m_fields.isEmpty() ? -1 : 0;
        frame.text = frame.fieldIndex >= 0 ? m_fields.at(frame.fieldIndex).name() : tr("Data Box");
        break;
    case CardTemplateFrameKind::NotesBox:
        for (int index = 0; index < m_fields.size(); ++index) {
            if (m_fields.at(index).isNotes()) {
                frame.fieldIndex = index;
                break;
            }
        }
        frame.text = frame.fieldIndex >= 0 ? m_fields.at(frame.fieldIndex).name() : tr("Notes");
        break;
    case CardTemplateFrameKind::LineOrBox:
        frame.text.clear();
        frame.lineBoxShape = CardTemplateLineBoxShape::Box;
        frame.bounds = QRect(left, top, 1800, 700);
        break;
    }
    return frame;
}

QString TemplateDesignerWidget::frameKindName(CardTemplateFrameKind kind) const
{
    switch (kind) {
    case CardTemplateFrameKind::Text:
        return tr("Text");
    case CardTemplateFrameKind::DataBox:
        return tr("Data Box");
    case CardTemplateFrameKind::NotesBox:
        return tr("Notes Box");
    case CardTemplateFrameKind::LineOrBox:
        return tr("Line/Box");
    }
    return tr("Unknown");
}

} // namespace CardStack
