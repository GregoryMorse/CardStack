#include "TemplateDesignerWidget.h"
#include "DesignerResizeHandles.h"
#include "DesignerFrameTable.h"
#include "DesignerSelection.h"
#include "DesignerInteractionGrid.h"
#include "DesignerClipboard.h"
#include <QItemSelectionModel>

#include "DeckSystemColors.h"
#include "FrameDrawingStyle.h"
#include "UiIds.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QFont>
#include <QFontMetricsF>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPalette>
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



QRect normalizedFrameBounds(QRect bounds, const CardTemplateLayout& layout)
{
    bounds.setWidth(std::max(1, bounds.width()));
    bounds.setHeight(std::max(1, bounds.height()));
    bounds.moveLeft(std::clamp(bounds.left(), 0, std::max(0, layout.canvasWidth - bounds.width())));
    bounds.moveTop(std::clamp(bounds.top(), 0, std::max(0, layout.canvasHeight - bounds.height())));
    return bounds;
}

QFont storedFont(const QString& serialized, const QFont& fallback)
{
    if (serialized.isEmpty()) {
        return fallback;
    }
    QFont font;
    return font.fromString(serialized) ? font : fallback;
}

void drawTemplateText(QPainter& painter, const QRectF& target, int flags, const QString& text,
                      const QFont& font, qreal sourcePixelScale)
{
    if (sourcePixelScale <= 0 || target.isEmpty()) return;
    painter.save();
    painter.translate(target.topLeft());
    painter.scale(sourcePixelScale, sourcePixelScale);
    painter.setFont(font);
    const QRectF local(0, 0, target.width() / sourcePixelScale, target.height() / sourcePixelScale);
    painter.setClipRect(local, Qt::IntersectClip);
    painter.drawText(local, flags, text);
    painter.restore();
}

void applyTemplateStyle(QFont* font, quint8 styleFlags)
{
    font->setBold((styleFlags & CardTemplateStyleFlagBold) != 0);
    font->setItalic((styleFlags & CardTemplateStyleFlagItalic) != 0);
    font->setUnderline((styleFlags & CardTemplateStyleFlagUnderline) != 0);
}

int captionFieldIndex(
    const CardTemplateFrame& textFrame,
    const CardTemplateLayout& layout,
    const QVector<FieldDefinition>& fields)
{
    if (textFrame.kind != CardTemplateFrameKind::Text) {
        return -1;
    }
    for (const CardTemplateFrame& dataFrame : layout.frames) {
        if (dataFrame.kind != CardTemplateFrameKind::DataBox
            && dataFrame.kind != CardTemplateFrameKind::NotesBox) {
            continue;
        }
        const int fieldIndex = dataFrame.fieldIndex;
        if (fieldIndex < 0 || fieldIndex >= fields.size() || !fields.at(fieldIndex).showName()) {
            continue;
        }
        const QString fieldCaption = dataFrame.text.trimmed().isEmpty()
            ? fields.at(fieldIndex).name()
            : dataFrame.text;
        const bool matchingText = textFrame.text.compare(fieldCaption, Qt::CaseInsensitive) == 0;
        const bool adjacentCaption = textFrame.bounds.left() < dataFrame.bounds.right()
            && textFrame.bounds.right() > dataFrame.bounds.left()
            && textFrame.bounds.bottom() <= dataFrame.bounds.top()
            && dataFrame.bounds.top() - textFrame.bounds.bottom() <= 300;
        if (matchingText || adjacentCaption) {
            return fieldIndex;
        }
    }
    return -1;
}

QColor templateColor(
    const DeckAppearance& appearance,
    DeckColorRole role,
    const QPalette& systemPalette)
{
    const int index = static_cast<int>(role);
    if (!appearance.useSystemColors
        && index >= 0
        && index < appearance.customColors.size()) {
        const QColor configured(appearance.customColors.at(index));
        if (configured.isValid()) {
            return configured;
        }
    }
    return deckSystemColor(role, systemPalette);
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
        setFocusPolicy(Qt::StrongFocus);
        setProperty("frameColor", QStringLiteral("#000000"));
        setProperty("fieldEditorBackground", QStringLiteral("#ececec"));
        setProperty("selectionColor", QStringLiteral("#808080"));
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

    void setAppearance(const DeckAppearance& appearance)
    {
        m_appearance = appearance;
        update();
    }

    int indexHeaderSourcePixels() const
    {
        if (m_appearance.indexFont.isEmpty()) return DesignerInteraction::DefaultIndexHeaderSourcePixels;
        return qCeil(QFontMetricsF(storedFont(m_appearance.indexFont, font())).height())
            + DesignerInteraction::IndexHeaderPaddingSourcePixels;
    }

    int selectionAnchor() const { return m_selectionAnchor; }
    void setSelectionAnchor(int anchor) { m_selectionAnchor = anchor; }

    void setSelectedFrames(QVector<int> indices)
    {
        m_selectedFrameIndices = DesignerSelection::normalized(std::move(indices), m_layout ? int(m_layout->frames.size()) : 0);
        m_selectedFrameIndex = m_selectedFrameIndices.isEmpty() ? -1 : m_selectedFrameIndices.last();
        m_selectionAnchor = m_selectedFrameIndex;
        update();
    }

    std::function<void(int)> frameSelected;
    std::function<void(const QVector<int>&)> framesSelected;
    std::function<void(const DesignerSelection::BoundsChanges&)> framesBoundsChanged;
    std::function<void()> deleteRequested;
    std::function<void(int)> frameActivated;
    std::function<void(int)> frameBoundsChangeStarted;
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
        painter.fillRect(
            page,
            templateColor(m_appearance, DeckColorRole::CardBackground, palette()));
        painter.setPen(QPen(Qt::black, 1));
        painter.drawRect(page.adjusted(0, 0, -1, -1));

        const QRectF indexHeader = scaledBounds(QRect(0, 0, m_layout->canvasWidth, qRound(indexHeaderSourcePixels() * DesignerInteraction::TemplateUnitsPerSourcePixel)), *m_layout, page);
        painter.save();
        painter.fillRect(
            indexHeader,
            templateColor(m_appearance, DeckColorRole::IndexBackground, palette()));
        const qreal logicalScale = page.width() / std::max(1, m_layout->canvasWidth);
        const qreal sourcePixelScale = logicalScale * DesignerInteraction::TemplateUnitsPerSourcePixel;
        painter.setPen(QPen(Qt::black, std::max<qreal>(1.0, 12.0 * logicalScale)));
        const qreal borderInset = std::max<qreal>(1.0, 6.0 * logicalScale);
        painter.drawRect(indexHeader.adjusted(borderInset, borderInset, -borderInset, -borderInset));
        QFont indexFont = storedFont(m_appearance.indexFont, painter.font());
        if (m_appearance.indexFont.isEmpty()) {
            indexFont.setPixelSize(std::max(8, qRound(indexHeaderSourcePixels() * 0.55)));
        }
        painter.setFont(indexFont);
        painter.setPen(
            templateColor(m_appearance, DeckColorRole::IndexForeground, palette()));
        const qreal textInset = std::max<qreal>(4.0, 30.0 * logicalScale);
        drawTemplateText(painter, indexHeader.adjusted(textInset, 0, -textInset, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         tr("Index"), indexFont, sourcePixelScale);
        painter.restore();

        for (int index = 0; index < m_layout->frames.size(); ++index) {
            const CardTemplateFrame& frame = m_layout->frames.at(index);
            const QRectF frameRect = scaledBounds(frame.bounds, *m_layout, page);
            painter.save();
            painter.setPen(QPen(Qt::black, 1));
            const bool nameCaption = m_fields != nullptr
                && captionFieldIndex(frame, *m_layout, *m_fields) >= 0;
            QFont frameFont = storedFont(
                nameCaption
                    ? m_appearance.nameFont
                    : frame.kind == CardTemplateFrameKind::Text ? m_appearance.textFont : m_appearance.dataFont,
                painter.font());
            applyTemplateStyle(&frameFont, frame.styleFlags);
            painter.setFont(frameFont);

            switch (frame.kind) {
            case CardTemplateFrameKind::LineOrBox: {
                ReportFrameDefinition drawing;
                drawing.lineBoxShape = frame.lineBoxShape == CardTemplateLineBoxShape::HorizontalLine
                    ? ReportLineShapeHorizontal : frame.lineBoxShape == CardTemplateLineBoxShape::VerticalLine
                        ? ReportLineShapeVertical : ReportLineShapeBox;
                drawing.lineStyle = frame.lineStyle;
                drawing.fillPattern = frame.fillPattern;
                drawing.cornerRadius = frame.cornerRadius;
                FrameDrawingStyle::draw(painter, frameRect, drawing, logicalScale);
                break;
            }
            case CardTemplateFrameKind::DataBox:
            case CardTemplateFrameKind::NotesBox: {
                painter.fillRect(
                    frameRect,
                    templateColor(
                        m_appearance,
                        DeckColorRole::DataBackground,
                        palette()));
                painter.setPen(QPen(Qt::black, 1));
                painter.drawRect(frameRect);
                painter.setPen(
                    templateColor(
                        m_appearance,
                        DeckColorRole::DataForeground,
                        palette()));
                QString previewText = frame.text;
                if (m_fields != nullptr
                    && frame.fieldIndex >= 0
                    && frame.fieldIndex < m_fields->size()) {
                    const FieldDefinition& field = m_fields->at(frame.fieldIndex);
                    previewText = field.showName() ? tr("Sample data") : field.name();
                }
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
                    painter.save();
                    QFont nameFont = storedFont(m_appearance.nameFont, painter.font());
                    applyTemplateStyle(&nameFont, frame.styleFlags);
                    painter.setFont(nameFont);
                    const QString fieldName = m_fields->at(frame.fieldIndex).name();
                    if (QFontMetricsF(nameFont).horizontalAdvance(fieldName) * sourcePixelScale <= labelRect.width()) {
                        painter.setPen(
                            templateColor(
                                m_appearance,
                                DeckColorRole::NameForeground,
                                palette()));
                        drawTemplateText(painter, labelRect, Qt::AlignRight | Qt::AlignVCenter, fieldName, nameFont, sourcePixelScale);
                    } else {
                        previewText = tr("%1: %2").arg(fieldName, previewText);
                    }
                    painter.restore();
                }
                drawTemplateText(painter, frameRect.adjusted(4, 2, -4, -2),
                                 Qt::AlignLeft | Qt::AlignVCenter, previewText, frameFont, sourcePixelScale);
                break;
            }
            case CardTemplateFrameKind::Text:
                painter.setPen(
                    templateColor(
                        m_appearance,
                        nameCaption ? DeckColorRole::NameForeground : DeckColorRole::TextForeground,
                        palette()));
                drawTemplateText(painter, frameRect,
                    ((frame.styleFlags & CardTemplateStyleFlagAlignRight) != 0
                            ? Qt::AlignRight
                            : ((frame.styleFlags & CardTemplateStyleFlagAlignCenter) != 0 ? Qt::AlignHCenter : Qt::AlignLeft))
                        | Qt::AlignVCenter, frame.text, frameFont, sourcePixelScale);
                break;
            }

            if (m_selectedFrameIndices.contains(index)) {
                const QColor selectionGray(QStringLiteral("#808080"));
                painter.setPen(QPen(selectionGray, 3, Qt::SolidLine));
                painter.drawRect(frameRect.adjusted(-2, -2, 1, 1));
                painter.setPen(QPen(selectionGray, 1, Qt::DotLine));
                painter.drawRect(frameRect.adjusted(3, 3, -4, -4));
                painter.setBrush(selectionGray);
                painter.setPen(Qt::NoPen);
                if (m_selectedFrameIndices.size() == 1) {
                    for (auto handle : DesignerResize::Handles)
                        painter.drawRect(DesignerResize::handleRect(frameRect, handle));
                }
                painter.setBrush(Qt::NoBrush);
            }
            painter.restore();
        }
        drawSelectionOverlay(painter);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier && deleteRequested) {
            deleteRequested();
            event->accept();
            return;
        }
        if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier) {
            QWidget::keyPressEvent(event);
            return;
        }
        QPoint delta;
        switch (event->key()) {
        case Qt::Key_Left: delta.setX(-1); break;
        case Qt::Key_Right: delta.setX(1); break;
        case Qt::Key_Up: delta.setY(-1); break;
        case Qt::Key_Down: delta.setY(1); break;
        default: QWidget::keyPressEvent(event); return;
        }
        if (!m_layout || m_selectedFrameIndices.isEmpty()) return;
        const QPoint pixels = delta * DesignerInteraction::SourceGridPixels;
        DesignerSelection::BoundsChanges values;
        if (event->modifiers() == Qt::ShiftModifier) {
            if (m_selectedFrameIndices.size() != 1) { event->accept(); return; }
            values.append({m_selectedFrameIndex, DesignerInteraction::TemplateGrid.resized(m_layout->frames.at(m_selectedFrameIndex).bounds,
                pixels, DesignerResize::Handle::BottomRight, QSize(m_layout->canvasWidth, m_layout->canvasHeight),
                DesignerInteraction::SourceGridPixels)});
        } else {
            DesignerSelection::BoundsChanges source;
            for (int index : m_selectedFrameIndices) source.append({index, m_layout->frames.at(index).bounds});
            values = DesignerSelection::translated(source,
                DesignerInteraction::TemplateGrid.modelDelta(DesignerInteraction::unionOf(source).topLeft(), pixels), QSize(m_layout->canvasWidth, m_layout->canvasHeight));
        }
        bool changed = false;
        for (const auto& value : values) changed |= m_layout->frames.at(value.first).bounds != value.second;
        if (changed) {
            if (frameBoundsChangeStarted) frameBoundsChangeStarted(m_selectedFrameIndex);
            if (framesBoundsChanged) framesBoundsChanged(values);
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_layout == nullptr) {
            QWidget::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const QRectF page = pageRect();
        const bool modifyingSelection = event->modifiers().testFlag(Qt::ShiftModifier)
            || event->modifiers().testFlag(Qt::ControlModifier);
        if (!modifyingSelection && m_selectedFrameIndices.size() == 1) {
            const QRectF selected = scaledBounds(m_layout->frames.at(m_selectedFrameIndex).bounds, *m_layout, page);
            const auto handle = DesignerResize::hitTest(selected, event->position());
            if (handle != DesignerResize::Handle::None) {
                startFrameDrag(m_selectedFrameIndex, event->position(), handle);
                event->accept();
                return;
            }
        }
        for (int index = m_layout->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page);
            if (!frameRect.contains(event->position())) continue;
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                const int anchor = m_selectionAnchor < 0 ? index : m_selectionAnchor;
                notifySelection(DesignerSelection::positionalRange(m_layout->frames, anchor, index), anchor);
            } else if (event->modifiers().testFlag(Qt::ControlModifier)) {
                QVector<int> selected = m_selectedFrameIndices;
                if (selected.contains(index)) selected.removeAll(index);
                else selected.append(index);
                notifySelection(selected, index);
            } else {
                if (!m_selectedFrameIndices.contains(index)) notifySelection({index}, index);
                startFrameDrag(index, event->position(), m_selectedFrameIndices.size() == 1
                    ? DesignerResize::hitTest(frameRect, event->position()) : DesignerResize::Handle::None);
            }
            event->accept();
            return;
        }
        notifySelection({}, -1);
        m_marqueeActive = page.contains(event->position());
        m_marqueeStart = m_marqueeEnd = event->position();
        update();
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_layout == nullptr) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_layout->frames.size() - 1; index >= 0; --index) {
            if (!scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page).contains(event->position())) {
                continue;
            }
            if (frameSelected) {
                frameSelected(index);
            }
            if (frameActivated) {
                frameActivated(index);
            }
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_layout) return;
        if (m_marqueeActive) {
            m_marqueeEnd = event->position();
            update();
            return;
        }
        if (m_dragMode != DragMode::None && m_dragFrameIndex >= 0) {
            const QPoint pixels = DesignerInteraction::TemplateGrid.pointerDelta(m_dragStartModelPosition, modelPoint(event->position()));
            DesignerSelection::BoundsChanges values;
            if (m_dragMode == DragMode::Move) {
                const QPoint origin = DesignerInteraction::unionOf(m_dragStartFrames).topLeft();
                values = DesignerSelection::translated(m_dragStartFrames, DesignerInteraction::TemplateGrid.modelDelta(origin, pixels),
                    QSize(m_layout->canvasWidth, m_layout->canvasHeight));
            } else {
                values.append({m_dragFrameIndex, DesignerInteraction::TemplateGrid.resized(m_dragStartBounds, pixels, m_resizeHandle,
                    QSize(m_layout->canvasWidth, m_layout->canvasHeight))});
            }
            bool changed = false;
            for (const auto& value : values) changed |= m_layout->frames.at(value.first).bounds != value.second;
            if (changed) {
                if (!m_dragChanged) {
                    if (frameBoundsChangeStarted) frameBoundsChangeStarted(m_dragFrameIndex);
                    m_dragChanged = true;
                }
                if (framesBoundsChanged) framesBoundsChanged(values);
            }
            return;
        }
        updateCursor(event->position());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        if (m_marqueeActive) {
            const QRectF selection = QRectF(m_marqueeStart, event->position()).normalized();
            QVector<int> selected;
            if (selection.width() > 0 && selection.height() > 0) {
                const QRectF page = pageRect();
                for (int index = 0; index < m_layout->frames.size(); ++index) {
                    if (selection.intersects(scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page))) selected.append(index);
                }
            }
            m_marqueeActive = false;
            notifySelection(selected, selected.isEmpty() ? -1 : selected.first());
        }
        m_dragMode = DragMode::None;
        m_dragFrameIndex = -1;
        m_dragChanged = false;
        m_resizeHandle = DesignerResize::Handle::None;
        updateCursor(event->position());
        update();
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

    QPointF modelPoint(QPointF position) const
    {
        const QRectF target = pageRect();
        return QPointF((position.x() - target.left()) * m_layout->canvasWidth / target.width(),
                       (position.y() - target.top()) * m_layout->canvasHeight / target.height());
    }

    void startFrameDrag(int frameIndex, const QPointF& position, DesignerResize::Handle handle)
    {
        setFocus(Qt::MouseFocusReason);
        m_dragFrameIndex = frameIndex;
        m_dragStartModelPosition = modelPoint(position);
        m_dragStartBounds = m_layout->frames.at(frameIndex).bounds;
        m_dragStartFrames.clear();
        for (int index : m_selectedFrameIndices) m_dragStartFrames.append({index, m_layout->frames.at(index).bounds});
        m_resizeHandle = handle;
        m_dragMode = handle == DesignerResize::Handle::None ? DragMode::Move : DragMode::Resize;
        m_dragChanged = false;
    }

    void updateCursor(const QPointF& position)
    {
        if (m_layout == nullptr) {
            unsetCursor();
            return;
        }
        const QRectF page = pageRect();
        if (m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout->frames.size()) {
            const auto handle = DesignerResize::hitTest(
                scaledBounds(m_layout->frames.at(m_selectedFrameIndex).bounds, *m_layout, page), position);
            if (handle != DesignerResize::Handle::None) {
                setCursor(DesignerResize::cursorFor(handle));
                return;
            }
        }
        for (int index = m_layout->frames.size() - 1; index >= 0; --index) {
            if (scaledBounds(m_layout->frames.at(index).bounds, *m_layout, page).contains(position)) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
        unsetCursor();
    }

    void notifySelection(const QVector<int>& selected, int anchor)
    {
        if (framesSelected) framesSelected(selected);
        m_selectionAnchor = anchor;
        update();
    }

    void drawSelectionOverlay(QPainter& painter) const
    {
        painter.save();
        if (m_selectedFrameIndices.size() > 1) {
            QRectF bounds;
            for (int index : m_selectedFrameIndices)
                bounds = bounds.united(scaledBounds(m_layout->frames.at(index).bounds, *m_layout, pageRect()));
            painter.setPen(QPen(QColor(QStringLiteral("#808080")), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bounds.adjusted(-3, -3, 3, 3));
        }
        if (m_marqueeActive) {
            QColor tint = palette().highlight().color();
            tint.setAlpha(24);
            painter.setPen(QPen(palette().highlight().color(), 1, Qt::DashLine));
            painter.setBrush(tint);
            painter.drawRect(QRectF(m_marqueeStart, m_marqueeEnd).normalized());
        }
        painter.restore();
    }

    const CardTemplateLayout* m_layout = nullptr;
    const QVector<FieldDefinition>* m_fields = nullptr;
    DeckAppearance m_appearance;
    int m_selectedFrameIndex = -1;
    QVector<int> m_selectedFrameIndices;
    int m_selectionAnchor = -1;
    bool m_marqueeActive = false;
    QPointF m_marqueeStart;
    QPointF m_marqueeEnd;
    DesignerSelection::BoundsChanges m_dragStartFrames;
    int m_dragFrameIndex = -1;
    DragMode m_dragMode = DragMode::None;
    DesignerResize::Handle m_resizeHandle = DesignerResize::Handle::None;
    bool m_dragChanged = false;
    QPointF m_dragStartModelPosition;
    QRect m_dragStartBounds;
};

TemplateDesignerWidget::TemplateDesignerWidget(
    CardTemplateLayout layout,
    QVector<FieldDefinition> fields,
    QWidget* parent,
    DeckAppearance appearance,
    QString description)
    : QWidget(parent)
    , m_layout(std::move(layout))
    , m_fields(std::move(fields))
    , m_appearance(std::move(appearance))
    , m_description(std::move(description))
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
    return m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size()
        ? &m_layout.frames.at(m_selectedFrameIndex)
        : nullptr;
}

int TemplateDesignerWidget::selectedFieldIndex() const
{
    if (m_selectedFrameIndices.size() != 1 || m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
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

bool TemplateDesignerWidget::addDataField(const QString& name, int length, bool phone, bool showName)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || length < 1 || std::any_of(m_fields.cbegin(), m_fields.cend(),
        [&cleanName](const auto& field) { return field.name().compare(cleanName, Qt::CaseInsensitive) == 0; })) return false;
    pushUndoState();
    m_fields.append(FieldDefinition(cleanName, FieldType::Text, length, showName, phone));
    const int fieldIndex = static_cast<int>(m_fields.size()) - 1;
    m_fieldCombo->addItem(cleanName, fieldIndex);
    CardTemplateFrame frame = defaultFrame(CardTemplateFrameKind::DataBox);
    frame.fieldIndex = fieldIndex;
    frame.text = cleanName;
    frame.bounds = normalizedFrameBounds(frame.bounds, m_layout);
    m_layout.frames.append(frame);
    refreshFrameTable();
    selectFrame(static_cast<int>(m_layout.frames.size()) - 1);
    markDirty();
    return true;
}

void TemplateDesignerWidget::addDataBoxFrame()
{
    QString name = tr("Data Box");
    int suffix = 2;
    while (std::any_of(m_fields.cbegin(), m_fields.cend(), [&name](const auto& field) {
        return field.name().compare(name, Qt::CaseInsensitive) == 0;
    })) name = tr("Data Box %1").arg(suffix++);
    addDataField(name, 255);
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
            m_fieldCombo->addItem(fieldName, frame.fieldIndex);
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
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Clear)) return;
    if (m_selectedFrameIndices.isEmpty()) return;
    applyPropertyEdits();
    pushUndoState();
    QVector<int> selected = m_selectedFrameIndices;
    std::sort(selected.begin(), selected.end(), std::greater<int>());
    const int next = *std::min_element(selected.cbegin(), selected.cend());
    m_selectedFrameIndices.clear();
    m_selectedFrameIndex = -1;
    for (int index : selected) m_layout.frames.removeAt(index);
    refreshFrameTable();
    selectFrame(std::min(next, int(m_layout.frames.size()) - 1));
    markDirty();
}

bool TemplateDesignerWidget::selectionHasStyledFrames() const
{
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_layout.frames.at(index);
        if (frame.kind == CardTemplateFrameKind::Text || frame.kind == CardTemplateFrameKind::DataBox) return true;
    }
    return false;
}

bool TemplateDesignerWidget::selectionHasStyleFlag(quint8 flag) const
{
    bool found = false;
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_layout.frames.at(index);
        if (!(frame.kind == CardTemplateFrameKind::Text || frame.kind == CardTemplateFrameKind::DataBox)) continue;
        found = true;
        if ((frame.styleFlags & flag) == 0) return false;
    }
    return found;
}

int TemplateDesignerWidget::selectionAlignment() const
{
    int result = -1;
    bool found = false;
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_layout.frames.at(index);
        if (!(frame.kind == CardTemplateFrameKind::Text || frame.kind == CardTemplateFrameKind::DataBox)) continue;
        const int alignment = frame.styleFlags & (CardTemplateStyleFlagAlignCenter | CardTemplateStyleFlagAlignRight);
        if (found && alignment != result) return -1;
        found = true;
        result = alignment;
    }
    return result;
}

void TemplateDesignerWidget::setSelectionStyleFlag(quint8 flag, bool enabled)
{
    applyPropertyEdits();
    const auto before = m_layout.frames;
    auto after = before;
    for (int index : m_selectedFrameIndices) {
        auto& frame = after[index];
        if (frame.kind == CardTemplateFrameKind::Text || frame.kind == CardTemplateFrameKind::DataBox) {
            if (enabled) frame.styleFlags |= flag;
            else frame.styleFlags &= ~flag;
        }
    }
    if (after == before) return;
    pushUndoState();
    m_layout.frames = std::move(after);
    refreshFrameProperties();
    refreshFrameTable();
    markDirty();
}

void TemplateDesignerWidget::setSelectionAlignment(quint8 alignment)
{
    applyPropertyEdits();
    const auto before = m_layout.frames;
    auto after = before;
    for (int index : m_selectedFrameIndices) {
        auto& frame = after[index];
        if (frame.kind == CardTemplateFrameKind::Text || frame.kind == CardTemplateFrameKind::DataBox)
            frame.styleFlags = (frame.styleFlags & ~(CardTemplateStyleFlagAlignCenter | CardTemplateStyleFlagAlignRight))
                | (alignment & (CardTemplateStyleFlagAlignCenter | CardTemplateStyleFlagAlignRight));
    }
    if (after == before) return;
    pushUndoState();
    m_layout.frames = std::move(after);
    refreshFrameProperties();
    refreshFrameTable();
    markDirty();
}

bool TemplateDesignerWidget::canCopyFrame() const
{
    if (DesignerClipboard::focusedEditor(this)) return DesignerClipboard::textSelection(this);
    return !m_selectedFrameIndices.isEmpty();
}

bool TemplateDesignerWidget::canPasteFrame() const
{
    if (DesignerClipboard::focusedEditor(this)) {
        const auto* mime = QApplication::clipboard()->mimeData();
        return mime && mime->hasText();
    }
    return !DesignerClipboard::readTemplate().frames.isEmpty();
}

bool TemplateDesignerWidget::copySelectionToClipboard()
{
    QVector<CardTemplateFrame> frames;
    QVector<int> selected = m_selectedFrameIndices;
    std::sort(selected.begin(), selected.end());
    for (int index : selected) frames.append(m_layout.frames.at(index));
    if (!frames.isEmpty()) {
        const QPoint delta = DesignerInteraction::TemplateGrid.copyDelta(DesignerInteraction::unionOfFrames(frames), m_canvas->indexHeaderSourcePixels());
        for (auto& frame : frames) frame.bounds.translate(delta);
    }
    return DesignerClipboard::writeTemplate(frames, m_fields);
}

void TemplateDesignerWidget::copySelectedFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Copy)) return;
    applyPropertyEdits();
    copySelectionToClipboard();
}

void TemplateDesignerWidget::cutSelectedFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Cut)) return;
    applyPropertyEdits();
    if (copySelectionToClipboard()) deleteSelectedFrame();
}

void TemplateDesignerWidget::refreshFieldChoices()
{
    const QSignalBlocker blocker(m_fieldCombo);
    m_fieldCombo->clear();
    m_fieldCombo->addItem(tr("(none)"), -1);
    for (int index = 0; index < m_fields.size(); ++index) m_fieldCombo->addItem(m_fields.at(index).name(), index);
}

void TemplateDesignerWidget::pasteFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Paste)) return;
    auto packet = DesignerClipboard::readTemplate();
    if (packet.frames.isEmpty()) return;
    applyPropertyEdits();
    pushUndoState();
    QMap<int, int> mapping;
    for (auto source = packet.fields.cbegin(); source != packet.fields.cend(); ++source) {
        const FieldDefinition& field = source.value();
        int target = -1;
        for (int index = 0; index < m_fields.size(); ++index) {
            const auto& candidate = m_fields.at(index);
            if (candidate.name().compare(field.name(), Qt::CaseInsensitive) == 0
                && candidate.type() == field.type() && candidate.maxLength() == field.maxLength()
                && candidate.showName() == field.showName() && candidate.isPhone() == field.isPhone()
                && candidate.displayWidth() == field.displayWidth()
                && candidate.legacyDescriptor() == field.legacyDescriptor()) { target = index; break; }
        }
        if (target < 0) {
            QString name = field.name();
            int suffix = 2;
            while (std::any_of(m_fields.cbegin(), m_fields.cend(), [&name](const auto& candidate) {
                return candidate.name().compare(name, Qt::CaseInsensitive) == 0;
            })) name = tr("%1 %2").arg(field.name()).arg(suffix++);
            target = int(m_fields.size());
            m_fields.append(FieldDefinition(name, field.type(), field.maxLength(), field.showName(),
                field.isPhone(), field.legacyDescriptor(), field.displayWidth()));
        }
        mapping.insert(source.key(), target);
    }
    DesignerSelection::BoundsChanges sourceBounds;
    for (int index = 0; index < packet.frames.size(); ++index) sourceBounds.append({index, packet.frames.at(index).bounds});
    const auto moved = DesignerSelection::translated(sourceBounds, QPoint(), QSize(m_layout.canvasWidth, m_layout.canvasHeight));
    m_selectedFrameIndices.clear();
    m_selectedFrameIndex = -1;
    QVector<int> selected;
    for (const auto& value : moved) {
        CardTemplateFrame frame = packet.frames.at(value.first);
        frame.bounds = value.second;
        if (frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox) {
            frame.fieldIndex = mapping.value(frame.fieldIndex, -1);
            if (frame.fieldIndex >= 0) frame.text = m_fields.at(frame.fieldIndex).name();
        } else if (frame.kind == CardTemplateFrameKind::Text) {
            for (auto field = packet.fields.cbegin(); field != packet.fields.cend(); ++field) {
                if (frame.text.compare(field.value().name(), Qt::CaseInsensitive) == 0)
                    frame.text = m_fields.at(mapping.value(field.key())).name();
            }
        }
        selected.append(int(m_layout.frames.size()));
        m_layout.frames.append(std::move(frame));
    }
    refreshFieldChoices();
    refreshFrameTable();
    selectFrames(selected);
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
        previous.legacyDescriptor(),
        previous.displayWidth());
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
    if (m_selectedFrameIndices.size() != 1 || m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) {
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

void TemplateDesignerWidget::setAppearance(DeckAppearance appearance)
{
    if (m_appearance == appearance) return;
    pushUndoState();
    m_appearance = std::move(appearance);
    if (m_canvas != nullptr) m_canvas->setAppearance(m_appearance);
    markDirty();
}

void TemplateDesignerWidget::markSaved()
{
    m_dirty = false;
    emit dirtyChanged(false);
}

void TemplateDesignerWidget::save()
{
    applyPropertyEdits();
    markSaved();
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
    m_canvas->setAppearance(m_appearance);
    splitter->addWidget(m_canvas);

    auto* side = new QWidget(splitter);
    auto* sideLayout = new QVBoxLayout(side);
    m_frameTable = new DesignerFrameTable(side);
    m_frameTable->setColumnCount(3);
    m_frameTable->setHorizontalHeaderLabels({tr("#"), tr("Kind"), tr("Text")});
    m_frameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sideLayout->addWidget(m_frameTable, 1);
    m_selectionSummary = new QLabel(side);
    m_selectionSummary->setObjectName(QStringLiteral("designerSelectionSummary"));
    m_selectionSummary->setWordWrap(true);
    m_selectionSummary->hide();
    sideLayout->addWidget(m_selectionSummary);

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

    connect(m_frameTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        QVector<int> selected;
        for (const QModelIndex& row : m_frameTable->selectionModel()->selectedRows()) selected.append(row.row());
        selectFrames(selected);
    });
    connect(m_frameTable, &QTableWidget::cellActivated, this, [this](int row, int) {
        if (m_selectedFrameIndices.size() > 1) return;
        selectFrame(row);
        emit commandRequested(UiIds::Command::ToolFrameAttributes);
    });
    m_canvas->frameSelected = [this](int frameIndex) { selectFrame(frameIndex); };
    m_canvas->deleteRequested = [this]() { deleteSelectedFrame(); };
    m_canvas->frameActivated = [this](int frameIndex) {
        selectFrame(frameIndex);
        emit commandRequested(UiIds::Command::ToolFrameAttributes);
    };
    m_canvas->framesSelected = [this](const QVector<int>& selected) { selectFrames(selected); };
    m_canvas->framesBoundsChanged = [this](const DesignerSelection::BoundsChanges& values) {
        for (const auto& value : values) {
            if (value.first < 0 || value.first >= m_layout.frames.size()) continue;
            m_layout.frames[value.first].bounds = value.second;
        }
        refreshFrameProperties();
        refreshFrameTable();
        markDirty();
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() { emit selectedFieldChanged(); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget*) { emit selectedFieldChanged(); });
    connect(m_textEdit, &QLineEdit::selectionChanged, this, [this]() { emit selectedFieldChanged(); });

    m_canvas->frameBoundsChangeStarted = [this](int) {
        pushUndoState();
    };
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
    syncFrameTableSelection();
    if (m_canvas != nullptr) {
        m_canvas->update();
    }
}

void TemplateDesignerWidget::refreshFrameProperties()
{
    const bool group = m_selectedFrameIndices.size() > 1;
    const bool hasFrame = m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_layout.frames.size();
    m_selectionSummary->setVisible(!hasFrame);
    if (group) m_selectionSummary->setText(tr("%1 frames selected. Drag the selection or use arrows to move the group.").arg(m_selectedFrameIndices.size()));
    else if (!hasFrame) m_selectionSummary->setText(tr("No frame selected. Click a frame or drag a rectangle to select frames."));
    for (int row = 0; row < m_propertyForm->rowCount(); ++row) m_propertyForm->setRowVisible(row, hasFrame);
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
    selectFrames(frameIndex >= 0 ? QVector<int>{frameIndex} : QVector<int>{});
}

void TemplateDesignerWidget::syncFrameTableSelection()
{
    if (!m_frameTable) return;
    QVector<int> current;
    for (const QModelIndex& row : m_frameTable->selectionModel()->selectedRows()) current.append(row.row());
    QVector<int> expected = m_selectedFrameIndices;
    std::sort(current.begin(), current.end());
    std::sort(expected.begin(), expected.end());
    if (current == expected) return;
    const QSignalBlocker blocker(m_frameTable);
    m_frameTable->clearSelection();
    if (m_selectedFrameIndex < 0) m_frameTable->setCurrentIndex(QModelIndex());
    if (m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_frameTable->rowCount())
        m_frameTable->setCurrentCell(m_selectedFrameIndex, 0, QItemSelectionModel::NoUpdate);
    for (int index : m_selectedFrameIndices)
        m_frameTable->selectionModel()->select(m_frameTable->model()->index(index, 0),
                                               QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void TemplateDesignerWidget::selectFrames(const QVector<int>& frameIndices)
{
    const QVector<int> normalized = DesignerSelection::normalized(frameIndices, int(m_layout.frames.size()));
    if (!m_restoringUndoState && normalized != m_selectedFrameIndices) applyPropertyEdits();
    m_selectedFrameIndices = normalized;
    m_selectedFrameIndex = normalized.isEmpty() ? -1 : normalized.last();
    syncFrameTableSelection();
    if (m_canvas) m_canvas->setSelectedFrames(normalized);
    refreshFrameProperties();
    emit selectedFieldChanged();
}

void TemplateDesignerWidget::setDescription(const QString& description)
{
    if (description == m_description) return;
    pushUndoState();
    m_description = description;
    markDirty();
}

void TemplateDesignerWidget::markDirty()
{
    emit propertiesChanged();
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
    m_undoStack.append({m_layout, m_fields, m_appearance, m_selectedFrameIndex, m_selectedFrameIndices, m_canvas ? m_canvas->selectionAnchor() : -1, m_description});
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
    m_appearance = state.appearance;
    m_description = state.description;
    refreshFieldChoices();
    if (m_canvas != nullptr) {
        m_canvas->setAppearance(m_appearance);
        m_canvas->setLayoutDefinition(&m_layout);
        m_canvas->setFieldDefinitions(&m_fields);
    }
    refreshFrameTable();
    selectFrames(state.selectedFrameIndices);
    if (m_canvas) m_canvas->setSelectionAnchor(state.selectionAnchor);
    m_restoringUndoState = false;
    markDirty();
}

void TemplateDesignerWidget::applyPropertyEdits()
{
    if (m_selectedFrameIndices.size() != 1 || m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_layout.frames.size()) return;
    const CardTemplateFrame previousFrame = m_layout.frames.at(m_selectedFrameIndex);
    pushUndoState();
    auto& frame = m_layout.frames[m_selectedFrameIndex];
    frame.kind = static_cast<CardTemplateFrameKind>(m_kindCombo->currentData().toInt());
    const bool fieldFrame = frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox;
    if (fieldFrame) {
        frame.fieldIndex = m_fieldCombo->currentData().toInt();
        if (frame.fieldIndex >= 0 && frame.fieldIndex < m_fields.size()) frame.text = m_fields.at(frame.fieldIndex).name();
    } else if (frame.kind == CardTemplateFrameKind::Text) frame.text = m_textEdit->text();
    frame.bounds = QRect(m_leftSpin->value(), m_topSpin->value(), m_widthSpin->value(), m_heightSpin->value());
    if (frame.kind == CardTemplateFrameKind::LineOrBox) {
        frame.lineBoxShape = lineBoxShapeFromInt(m_lineShapeCombo->currentData().toInt());
        frame.lineStyle = m_lineStyleSpin->value();
        frame.fillPattern = m_fillPatternSpin->value();
        frame.cornerRadius = m_cornerRadiusSpin->value();
    } else {
        constexpr quint8 EditableStyleMask = CardTemplateStyleFlagBold | CardTemplateStyleFlagItalic | CardTemplateStyleFlagUnderline;
        quint8 flags = frame.styleFlags & ~EditableStyleMask;
        if (m_boldCheck->isChecked()) flags |= CardTemplateStyleFlagBold;
        if (m_italicCheck->isChecked()) flags |= CardTemplateStyleFlagItalic;
        if (m_underlineCheck->isChecked()) flags |= CardTemplateStyleFlagUnderline;
        frame.styleFlags = flags;
    }
    if (frame == previousFrame && !m_undoStack.isEmpty()) m_undoStack.removeLast();
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
