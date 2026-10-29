#include "ReportDesignerWidget.h"
#include "DesignerResizeHandles.h"
#include "DesignerFrameTable.h"
#include "DesignerSelection.h"
#include "DesignerInteractionGrid.h"
#include "DesignerClipboard.h"
#include "ReportDesignerViewport.h"
#include <QItemSelectionModel>

#include "ReportPreviewRenderer.h"
#include "ReportPrintEngine.h"
#include "ReportStyleCatalog.h"
#include "UiIds.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace CardStack {
namespace {

QStringList lineStyleNames()
{
    return ReportStyleCatalog::lineStyleNames();
}

QStringList fillPatternNames()
{
    return ReportStyleCatalog::fillPatternNames();
}

QString kindName(ReportFrameKind kind)
{
    switch (kind) {
    case ReportFrameKind::Text:
        return QObject::tr("Text");
    case ReportFrameKind::Data:
        return QObject::tr("Data");
    case ReportFrameKind::SystemText:
        return QObject::tr("System");
    case ReportFrameKind::LineOrBox:
        return QObject::tr("Line/Box");
    case ReportFrameKind::Unknown:
        break;
    }
    return QObject::tr("Unknown");
}

QString displayTextForFrame(const ReportFrameDefinition& frame)
{
    if (!frame.text.trimmed().isEmpty()) {
        return frame.text.simplified();
    }
    return kindName(frame.kind);
}

QSizeF designPageSize(const ReportDefinition& report)
{
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label)
        return QSizeF(std::max(1, report.formWidth), std::max(1, report.formHeight));
    return ReportPrintEngine::pageSizeMils(report);
}

QRectF designContentRect(const ReportDefinition& report, const QRectF& page)
{
    // A card/label designer edits one cell, not the sheet that arranges those cells.
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label) return page;
    const QSizeF paper = designPageSize(report);
    const QRectF content = page.adjusted(report.marginLeft * page.width() / paper.width(),
                                         report.marginTop * page.height() / paper.height(),
                                         -report.marginRight * page.width() / paper.width(),
                                         -report.marginBottom * page.height() / paper.height());
    return content.isValid() && !content.isEmpty() ? content : page;
}

QRectF scaledBounds(const QRect& bounds, const ReportDefinition& report, const QRectF& page)
{
    const qreal width = std::max(1, report.formWidth);
    const qreal height = std::max(1, report.formHeight);
    const QRectF content = designContentRect(report, page);
    return QRectF(content.left() + bounds.left() * content.width() / width,
                  content.top() + bounds.top() * content.height() / height,
                  bounds.width() * content.width() / width,
                  bounds.height() * content.height() / height);
}





int lineBoxShapeValue(ReportLineBoxShape shape)
{
    switch (shape) {
    case ReportLineBoxShape::HorizontalLine:
        return ReportLineShapeHorizontal;
    case ReportLineBoxShape::VerticalLine:
        return ReportLineShapeVertical;
    case ReportLineBoxShape::Box:
        break;
    }
    return ReportLineShapeBox;
}

} // namespace

class ReportDesignCanvas : public ReportDesignerViewport {
public:
    explicit ReportDesignCanvas(QWidget* parent = nullptr)
        : ReportDesignerViewport(parent)
    {
        setObjectName(QStringLiteral("reportDesignCanvas"));
        setMinimumSize(360, 420);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setProperty("hasHorizontalRuler", true);
        setProperty("hasVerticalRuler", true);
        setProperty("guideColor", QStringLiteral("#0057b8"));
        setProperty("selectionColor", QStringLiteral("#c51f1f"));
        setProperty("guideLineStyle", QStringLiteral("dotted"));
    }

    void setReport(const ReportDefinition* report)
    {
        m_report = report;
        if (report) {
            const QSizeF size = designPageSize(*report);
            setDocumentSize(QSize(qRound(size.width()), qRound(size.height())));
        }
        update();
    }

    int selectionAnchor() const { return m_selectionAnchor; }
    void setSelectionAnchor(int anchor) { m_selectionAnchor = anchor; }

    void setSelectedFrames(QVector<int> indices)
    {
        m_selectedFrameIndices = DesignerSelection::normalized(std::move(indices), m_report ? int(m_report->frames.size()) : 0);
        m_selectedFrameIndex = m_selectedFrameIndices.isEmpty() ? -1 : m_selectedFrameIndices.last();
        m_selectionAnchor = m_selectedFrameIndex;
        revealSelection();
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
        if (m_report == nullptr) {
            return;
        }

        const QRectF page = pageRect();
        painter.save();
        painter.setClipRect(contentViewportRect());
        painter.fillRect(page.translated(5.0, 5.0), QColor(0, 0, 0, 35));
        ReportPreviewData previewData;
        for (const ReportFrameDefinition& frame : m_report->frames) {
            for (const QString& field : frame.fieldPlaceholders) {
                previewData.fieldValues.insert(field, QStringLiteral("[%1]").arg(field));
            }
            for (const QString& token : frame.systemTokens) {
                previewData.systemValues.insert(token, QStringLiteral("{%1}").arg(token));
            }
        }
        painter.fillRect(page, Qt::white);
        ReportPreviewRenderer::render(&painter, *m_report, designContentRect(*m_report, page), previewData);

        const QColor guideBlue(QStringLiteral("#0057b8"));
        const QColor selectionRed(QStringLiteral("#c51f1f"));
        painter.setPen(QPen(guideBlue, 1, Qt::SolidLine));
        painter.drawRect(page.adjusted(0.0, 0.0, -1.0, -1.0));

        const QRectF printableArea = designContentRect(*m_report, page);
        const qreal yScale = printableArea.height() / std::max(1, m_report->formHeight);
        if (printableArea.isValid() && !printableArea.isEmpty()) {
            painter.setPen(QPen(guideBlue, 1, Qt::DotLine));
            painter.drawRect(printableArea.adjusted(0.0, 0.0, -1.0, -1.0));
            if (m_report->formType == ReportFormType::Report) {
                const qreal headerMils = m_report->headerHeight;
                const qreal footerMils = m_report->footerHeight;
                const qreal headerY = printableArea.top() + headerMils * yScale;
                const qreal footerY = printableArea.bottom() - footerMils * yScale;
                if (headerY > printableArea.top() && headerY < printableArea.bottom()) {
                    painter.drawLine(QPointF(printableArea.left(), headerY),
                                     QPointF(printableArea.right(), headerY));
                }
                if (footerY > printableArea.top() && footerY < printableArea.bottom()) {
                    painter.drawLine(QPointF(printableArea.left(), footerY),
                                     QPointF(printableArea.right(), footerY));
                }
            }
        }

        for (int index = 0; index < m_report->frames.size(); ++index) {
            const QRectF frameRect = scaledBounds(m_report->frames.at(index).bounds, *m_report, page);
            const bool selected = m_selectedFrameIndices.contains(index);
            painter.setPen(selected
                               ? QPen(selectionRed, 2, Qt::SolidLine)
                               : QPen(guideBlue, 1, Qt::DotLine));
            painter.drawRect(frameRect.adjusted(0.0, 0.0, -1.0, -1.0));
            if (selected) {
                painter.setPen(QPen(selectionRed, 1, Qt::DotLine));
                painter.drawRect(frameRect.adjusted(3.0, 3.0, -4.0, -4.0));
                painter.setBrush(selectionRed);
                painter.setPen(Qt::NoPen);
                if (m_selectedFrameIndices.size() == 1) {
                    for (auto handle : DesignerResize::Handles)
                        painter.drawRect(DesignerResize::handleRect(frameRect, handle));
                }
                painter.setBrush(Qt::NoBrush);
            }
        }
        drawSelectionOverlay(painter);
        painter.restore();
        drawRulers(painter, page);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (handleViewportKey(event)) return;
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
        if (!m_report || m_selectedFrameIndices.isEmpty()) return;
        const QPoint pixels = delta * DesignerInteraction::SourceGridPixels;
        DesignerSelection::BoundsChanges values;
        if (event->modifiers() == Qt::ShiftModifier) {
            if (m_selectedFrameIndices.size() != 1) { event->accept(); return; }
            values.append({m_selectedFrameIndex, DesignerInteraction::ReportGrid.resized(m_report->frames.at(m_selectedFrameIndex).bounds,
                pixels, DesignerResize::Handle::BottomRight, QSize(m_report->formWidth, m_report->formHeight),
                DesignerInteraction::SourceGridPixels)});
        } else {
            DesignerSelection::BoundsChanges source;
            for (int index : m_selectedFrameIndices) source.append({index, m_report->frames.at(index).bounds});
            values = DesignerSelection::translated(source,
                DesignerInteraction::ReportGrid.modelDelta(DesignerInteraction::unionOf(source).topLeft(), pixels), QSize(m_report->formWidth, m_report->formHeight));
        }
        bool changed = false;
        for (const auto& value : values) changed |= m_report->frames.at(value.first).bounds != value.second;
        if (changed) {
            if (frameBoundsChangeStarted) frameBoundsChangeStarted(m_selectedFrameIndex);
            if (framesBoundsChanged) framesBoundsChanged(values);
            revealSelection();
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_report == nullptr) {
            QWidget::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        if (!contentViewportRect().contains(event->position().toPoint())) { event->accept(); return; }
        const QRectF page = pageRect();
        const bool modifyingSelection = event->modifiers().testFlag(Qt::ShiftModifier)
            || event->modifiers().testFlag(Qt::ControlModifier);
        if (!modifyingSelection && m_selectedFrameIndices.size() == 1) {
            const QRectF selected = scaledBounds(m_report->frames.at(m_selectedFrameIndex).bounds, *m_report, page);
            const auto handle = DesignerResize::hitTest(selected, event->position());
            if (handle != DesignerResize::Handle::None) {
                startFrameDrag(m_selectedFrameIndex, event->position(), handle);
                event->accept();
                return;
            }
        }
        for (int index = m_report->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_report->frames.at(index).bounds, *m_report, page);
            if (!frameRect.contains(event->position())) continue;
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                const int anchor = m_selectionAnchor < 0 ? index : m_selectionAnchor;
                notifySelection(DesignerSelection::positionalRange(m_report->frames, anchor, index), anchor);
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
        if (event->button() != Qt::LeftButton || m_report == nullptr) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_report->frames.size() - 1; index >= 0; --index) {
            if (!scaledBounds(m_report->frames.at(index).bounds, *m_report, page).contains(event->position())) {
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
        if (!m_report) return;
        if (m_marqueeActive) {
            m_marqueeEnd = event->position();
            update();
            return;
        }
        if (m_dragMode != DragMode::None && m_dragFrameIndex >= 0) {
            const QPoint pixels = DesignerInteraction::ReportGrid.pointerDelta(m_dragStartModelPosition, modelPoint(event->position()));
            DesignerSelection::BoundsChanges values;
            if (m_dragMode == DragMode::Move) {
                const QPoint origin = DesignerInteraction::unionOf(m_dragStartFrames).topLeft();
                values = DesignerSelection::translated(m_dragStartFrames, DesignerInteraction::ReportGrid.modelDelta(origin, pixels),
                    QSize(m_report->formWidth, m_report->formHeight));
            } else {
                values.append({m_dragFrameIndex, DesignerInteraction::ReportGrid.resized(m_dragStartBounds, pixels, m_resizeHandle,
                    QSize(m_report->formWidth, m_report->formHeight))});
            }
            bool changed = false;
            for (const auto& value : values) changed |= m_report->frames.at(value.first).bounds != value.second;
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
                for (int index = 0; index < m_report->frames.size(); ++index) {
                    if (selection.intersects(scaledBounds(m_report->frames.at(index).bounds, *m_report, page))) selected.append(index);
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
    static constexpr int RulerThickness = 28;
    static constexpr int MinorTickMils = 125;
    static constexpr int MajorTickMils = 1000;

    enum class DragMode {
        None,
        Move,
        Resize
    };

    void drawRulers(QPainter& painter, const QRectF& page) const
    {
        const QRect area = drawableRect();
        const QColor background(QStringLiteral("#f5df72"));
        const QColor edge(QStringLiteral("#8f7c2b"));
        painter.save();
        painter.setClipRect(area);
        painter.fillRect(QRect(0, 0, area.width(), RulerThickness), background);
        painter.fillRect(QRect(0, 0, RulerThickness, area.height()), background);
        painter.setPen(edge);
        painter.drawLine(0, RulerThickness - 1, area.right(), RulerThickness - 1);
        painter.drawLine(RulerThickness - 1, 0, RulerThickness - 1, area.bottom());
        if (m_report) {
            const QSizeF size = designPageSize(*m_report);
            const int width = std::max(1, qRound(size.width()));
            const int height = std::max(1, qRound(size.height()));
            painter.setPen(QColor(QStringLiteral("#3f3615")));
            for (int axis = 0; axis < 2; ++axis) {
                const bool horizontal = axis == 0;
                const int units = horizontal ? width : height;
                painter.save();
                painter.setClipRect(horizontal ? QRect(RulerThickness, 0, area.width() - RulerThickness, RulerThickness)
                                              : QRect(0, RulerThickness, RulerThickness, area.height() - RulerThickness));
                for (int value = 0; value <= units; value += MinorTickMils) {
                    const qreal position = horizontal ? page.left() + page.width() * value / width
                                                     : page.top() + page.height() * value / height;
                    const bool major = value % MajorTickMils == 0;
                    const bool half = value % (MajorTickMils / 2) == 0;
                    const int length = major ? 11 : half ? 8 : 5;
                    if (horizontal)
                        painter.drawLine(QPointF(position, RulerThickness - 1), QPointF(position, RulerThickness - 1 - length));
                    else
                        painter.drawLine(QPointF(RulerThickness - 1, position), QPointF(RulerThickness - 1 - length, position));
                }
                painter.restore();
            }
            const auto labels = ReportDesignerRulers::labels(page, QSize(width, height), area, QFontMetricsF(painter.font()));
            for (const auto& label : labels) painter.drawText(label.bounds, label.alignment, label.text);
        }
        painter.fillRect(QRect(0, 0, RulerThickness - 1, RulerThickness - 1), background);
        painter.restore();
    }

    QRectF pageRect() const
    {
        return documentPageRect();
    }

    QPointF modelPoint(QPointF position) const
    {
        const QRectF target = designContentRect(*m_report, pageRect());
        return QPointF((position.x() - target.left()) * m_report->formWidth / target.width(),
                       (position.y() - target.top()) * m_report->formHeight / target.height());
    }

    void revealSelection()
    {
        if (!m_report || m_selectedFrameIndices.isEmpty()) return;
        QRectF bounds;
        for (int index : m_selectedFrameIndices)
            bounds = bounds.united(scaledBounds(m_report->frames.at(index).bounds, *m_report, pageRect()));
        ensureDocumentRectVisible(bounds);
    }

    void startFrameDrag(int frameIndex, const QPointF& position, DesignerResize::Handle handle)
    {
        setFocus(Qt::MouseFocusReason);
        m_dragFrameIndex = frameIndex;
        m_dragStartModelPosition = modelPoint(position);
        m_dragStartBounds = m_report->frames.at(frameIndex).bounds;
        m_dragStartFrames.clear();
        for (int index : m_selectedFrameIndices) m_dragStartFrames.append({index, m_report->frames.at(index).bounds});
        m_resizeHandle = handle;
        m_dragMode = handle == DesignerResize::Handle::None ? DragMode::Move : DragMode::Resize;
        m_dragChanged = false;
    }

    void updateCursor(const QPointF& position)
    {
        if (m_report == nullptr) {
            unsetCursor();
            return;
        }
        const QRectF page = pageRect();
        if (m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report->frames.size()) {
            const auto handle = DesignerResize::hitTest(
                scaledBounds(m_report->frames.at(m_selectedFrameIndex).bounds, *m_report, page), position);
            if (handle != DesignerResize::Handle::None) {
                setCursor(DesignerResize::cursorFor(handle));
                return;
            }
        }
        for (int index = m_report->frames.size() - 1; index >= 0; --index) {
            if (scaledBounds(m_report->frames.at(index).bounds, *m_report, page).contains(position)) {
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
                bounds = bounds.united(scaledBounds(m_report->frames.at(index).bounds, *m_report, pageRect()));
            painter.setPen(QPen(QColor(QStringLiteral("#c51f1f")), 1, Qt::DashLine));
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

    const ReportDefinition* m_report = nullptr;
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

ReportDesignerWidget::ReportDesignerWidget(ReportDefinition report, QStringList fieldNames, QWidget* parent)
    : QWidget(parent)
    , m_report(std::move(report))
    , m_fieldNames(std::move(fieldNames))
{
    normalizeReport();
    buildUi();
    refreshFrameTable();
    selectFrame(m_report.frames.isEmpty() ? -1 : 0);
}

const ReportDefinition& ReportDesignerWidget::report() const
{
    return m_report;
}

int ReportDesignerWidget::selectedFrameIndex() const
{
    return m_selectedFrameIndex;
}

const ReportFrameDefinition* ReportDesignerWidget::selectedFrameDefinition() const
{
    return m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report.frames.size()
        ? &m_report.frames.at(m_selectedFrameIndex)
        : nullptr;
}

QStringList ReportDesignerWidget::fieldNames() const
{
    return m_fieldNames;
}

bool ReportDesignerWidget::isDirty() const
{
    return m_dirty;
}

void ReportDesignerWidget::commitPendingEdits()
{
    applyPropertyEdits();
    m_report.name = m_nameEdit == nullptr ? m_report.name : m_nameEdit->text().trimmed();
    if (m_report.name.isEmpty()) {
        m_report.name = tr("Untitled Report");
    }
}

void ReportDesignerWidget::setReportName(const QString& name)
{
    pushUndoState();
    const QString trimmedName = name.trimmed();
    m_report.name = trimmedName.isEmpty() ? tr("Untitled Report") : trimmedName;
    if (m_nameEdit != nullptr) {
        m_nameEdit->setText(m_report.name);
    }
    markDirty();
}

void ReportDesignerWidget::applyForm(
    ReportFormType formType,
    int formWidth,
    int formHeight,
    int rows,
    int columns,
    int marginLeft,
    int marginTop,
    int marginRight,
    int marginBottom,
    int horizontalGutter,
    int verticalGutter,
    int headerHeight,
    int footerHeight,
    int pageWidth, int pageHeight, int orientation, int paperStyleId)
{
    pushUndoState();
    m_report.formType = formType == ReportFormType::Unknown ? ReportFormType::Report : formType;
    m_report.formWidth = std::max(1, formWidth);
    m_report.formHeight = std::max(1, formHeight);
    m_report.rows = std::max(1, rows);
    m_report.columns = std::max(1, columns);
    m_report.marginLeft = std::max(0, marginLeft);
    m_report.marginTop = std::max(0, marginTop);
    m_report.marginRight = std::max(0, marginRight);
    m_report.marginBottom = std::max(0, marginBottom);
    m_report.horizontalGutter = std::max(0, horizontalGutter);
    m_report.verticalGutter = std::max(0, verticalGutter);
    if (pageWidth > 0) m_report.pageWidth = pageWidth;
    if (pageHeight > 0) m_report.pageHeight = pageHeight;
    if (orientation >= 0) m_report.orientation = orientation;
    if (paperStyleId > 0) m_report.paperStyleId = paperStyleId;
    m_report.headerHeight = m_report.formType == ReportFormType::Report
        ? std::max(0, headerHeight)
        : 0;
    m_report.footerHeight = m_report.formType == ReportFormType::Report
        ? std::max(0, footerHeight)
        : 0;
    classifyReportBands(&m_report);
    if (m_canvas != nullptr) {
        m_canvas->setReport(&m_report);
    }
    markDirty();
}

void ReportDesignerWidget::addTextFrame()
{
    pushUndoState();
    m_report.frames.append(defaultFrame(ReportFrameKind::Text));
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addTextFrameWithText(const QString& text, quint8 styleFlags)
{
    pushUndoState();
    ReportFrameDefinition frame = defaultFrame(ReportFrameKind::Text);
    if (!text.trimmed().isEmpty()) {
        frame.text = text.trimmed();
    }
    frame.styleFlags = styleFlags;
    m_report.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addDataFrame()
{
    pushUndoState();
    m_report.frames.append(defaultFrame(ReportFrameKind::Data));
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addDataFrameForField(const QString& fieldName, quint8 styleFlags, bool printEntireContents)
{
    pushUndoState();
    const QString safeFieldName = fieldName.trimmed().isEmpty()
        ? (m_fieldNames.isEmpty() ? tr("Field") : m_fieldNames.first())
        : fieldName.trimmed();
    ReportFrameDefinition frame = defaultFrame(ReportFrameKind::Data);
    frame.text = QStringLiteral("[%1]").arg(safeFieldName);
    frame.fieldPlaceholders = {safeFieldName};
    frame.styleFlags = styleFlags;
    frame.printEntireContentsFlag = printEntireContents ? 1 : 0;
    m_report.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addSystemFrame()
{
    pushUndoState();
    m_report.frames.append(defaultFrame(ReportFrameKind::SystemText));
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addSystemFrameWithText(const QString& text, quint8 styleFlags)
{
    pushUndoState();
    QString tokenText = text.trimmed();
    if (tokenText.isEmpty()) {
        tokenText = QStringLiteral("{page}");
    }

    ReportFrameDefinition frame = defaultFrame(ReportFrameKind::SystemText);
    frame.text = tokenText;
    frame.systemTokens.clear();
    const QRegularExpression tokenPattern(QStringLiteral("\\{([^}]+)\\}"));
    QRegularExpressionMatchIterator matches = tokenPattern.globalMatch(frame.text);
    while (matches.hasNext()) {
        frame.systemTokens.append(matches.next().captured(1));
    }
    if (frame.systemTokens.isEmpty()) {
        frame.text = QStringLiteral("{%1}").arg(tokenText);
        frame.systemTokens = {tokenText};
    }
    frame.styleFlags = styleFlags;
    m_report.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addLineBoxFrame()
{
    pushUndoState();
    m_report.frames.append(defaultFrame(ReportFrameKind::LineOrBox));
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::addLineBoxFrameShape(ReportLineBoxShape shape, int lineStyle, int fillPattern, int cornerRadius)
{
    pushUndoState();
    ReportFrameDefinition frame = defaultFrame(ReportFrameKind::LineOrBox);
    frame.lineBoxShape = lineBoxShapeValue(shape);
    frame.lineStyle = std::clamp(lineStyle, 0, ReportLineStyleCount - 1);
    frame.fillPattern = std::clamp(fillPattern, 0, ReportFillPatternCount - 1);
    frame.cornerRadius = std::max(0, cornerRadius);
    switch (shape) {
    case ReportLineBoxShape::HorizontalLine:
        frame.bounds.setHeight(40);
        break;
    case ReportLineBoxShape::VerticalLine:
        frame.bounds.setWidth(40);
        break;
    case ReportLineBoxShape::Box:
        break;
    }

    m_report.frames.append(frame);
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
    markDirty();
}

void ReportDesignerWidget::deleteSelectedFrame()
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
    for (int index : selected) m_report.frames.removeAt(index);
    refreshFrameTable();
    selectFrame(std::min(next, int(m_report.frames.size()) - 1));
    markDirty();
}

bool ReportDesignerWidget::selectionHasStyledFrames() const
{
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_report.frames.at(index);
        if (frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data || frame.kind == ReportFrameKind::SystemText) return true;
    }
    return false;
}

bool ReportDesignerWidget::selectionHasStyleFlag(quint8 flag) const
{
    bool found = false;
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_report.frames.at(index);
        if (!(frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data || frame.kind == ReportFrameKind::SystemText)) continue;
        found = true;
        if ((frame.styleFlags & flag) == 0) return false;
    }
    return found;
}

int ReportDesignerWidget::selectionAlignment() const
{
    int result = -1;
    bool found = false;
    for (int index : m_selectedFrameIndices) {
        const auto& frame = m_report.frames.at(index);
        if (!(frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data || frame.kind == ReportFrameKind::SystemText)) continue;
        const int alignment = frame.styleFlags & (ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight);
        if (found && alignment != result) return -1;
        found = true;
        result = alignment;
    }
    return result;
}

void ReportDesignerWidget::setSelectionStyleFlag(quint8 flag, bool enabled)
{
    applyPropertyEdits();
    const auto before = m_report.frames;
    auto after = before;
    for (int index : m_selectedFrameIndices) {
        auto& frame = after[index];
        if (frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data || frame.kind == ReportFrameKind::SystemText) {
            if (enabled) frame.styleFlags |= flag;
            else frame.styleFlags &= ~flag;
        }
    }
    if (after == before) return;
    pushUndoState();
    m_report.frames = std::move(after);
    refreshFrameProperties();
    refreshFrameTable();
    markDirty();
}

void ReportDesignerWidget::setSelectionAlignment(quint8 alignment)
{
    applyPropertyEdits();
    const auto before = m_report.frames;
    auto after = before;
    for (int index : m_selectedFrameIndices) {
        auto& frame = after[index];
        if (frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data || frame.kind == ReportFrameKind::SystemText)
            frame.styleFlags = (frame.styleFlags & ~(ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight))
                | (alignment & (ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight));
    }
    if (after == before) return;
    pushUndoState();
    m_report.frames = std::move(after);
    refreshFrameProperties();
    refreshFrameTable();
    markDirty();
}

bool ReportDesignerWidget::canCopyFrame() const
{
    if (DesignerClipboard::focusedEditor(this)) return DesignerClipboard::textSelection(this);
    return !m_selectedFrameIndices.isEmpty();
}

bool ReportDesignerWidget::canPasteFrame() const
{
    if (DesignerClipboard::focusedEditor(this)) {
        const auto* mime = QApplication::clipboard()->mimeData();
        return mime && mime->hasText();
    }
    return !DesignerClipboard::readReport().isEmpty();
}

bool ReportDesignerWidget::copySelectionToClipboard()
{
    QVector<ReportFrameDefinition> frames;
    QVector<int> selected = m_selectedFrameIndices;
    std::sort(selected.begin(), selected.end());
    for (int index : selected) frames.append(m_report.frames.at(index));
    if (!frames.isEmpty()) {
        const QPoint delta = DesignerInteraction::ReportGrid.copyDelta(DesignerInteraction::unionOfFrames(frames));
        for (auto& frame : frames) frame.bounds.translate(delta);
    }
    return DesignerClipboard::writeReport(frames);
}

void ReportDesignerWidget::copySelectedFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Copy)) return;
    commitPendingEdits();
    copySelectionToClipboard();
}

void ReportDesignerWidget::cutSelectedFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Cut)) return;
    applyPropertyEdits();
    if (copySelectionToClipboard()) deleteSelectedFrame();
}

void ReportDesignerWidget::pasteFrame()
{
    if (DesignerClipboard::textCommand(this, DesignerClipboard::TextCommand::Paste)) return;
    QVector<ReportFrameDefinition> frames = DesignerClipboard::readReport();
    if (frames.isEmpty()) return;
    applyPropertyEdits();
    pushUndoState();
    DesignerSelection::BoundsChanges source;
    for (int index = 0; index < frames.size(); ++index) source.append({index, frames.at(index).bounds});
    const auto moved = DesignerSelection::translated(source, QPoint(), QSize(m_report.formWidth, m_report.formHeight));
    m_selectedFrameIndices.clear();
    m_selectedFrameIndex = -1;
    QVector<int> selected;
    for (const auto& value : moved) {
        auto frame = frames.at(value.first);
        frame.bounds = value.second;
        frame.band = reportBandForBounds(m_report, frame.bounds);
        selected.append(int(m_report.frames.size()));
        m_report.frames.append(std::move(frame));
    }
    refreshFrameTable();
    selectFrames(selected);
    markDirty();
}

void ReportDesignerWidget::selectCurrentFrameText()
{
    if (m_textEdit != nullptr && m_textEdit->isEnabled()) {
        m_textEdit->setFocus();
        m_textEdit->selectAll();
    }
}

void ReportDesignerWidget::updateSelectedFrameFromToolbar(
    const QString& text,
    quint8 styleFlags,
    bool printEntireContents,
    int lineBoxShape,
    int lineStyle,
    int fillPattern,
    int cornerRadius)
{
    if (m_selectedFrameIndices.size() != 1 || m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_report.frames.size()) {
        return;
    }
    pushUndoState();
    ReportFrameDefinition& frame = m_report.frames[m_selectedFrameIndex];
    if (frame.kind == ReportFrameKind::LineOrBox) {
        frame.lineBoxShape = lineBoxShape;
        frame.lineStyle = std::clamp(lineStyle, 0, ReportLineStyleCount - 1);
        frame.fillPattern = std::clamp(fillPattern, 0, ReportFillPatternCount - 1);
        frame.cornerRadius = std::max(0, cornerRadius);
    } else {
        frame.text = text;
        frame.styleFlags = styleFlags;
        frame.printEntireContentsFlag = printEntireContents ? 1 : 0;
        frame.fieldPlaceholders.clear();
        frame.systemTokens.clear();
        if (frame.kind == ReportFrameKind::Data) {
            const QRegularExpression pattern(QStringLiteral("\\[([^\\]]+)\\]"));
            QRegularExpressionMatchIterator matches = pattern.globalMatch(frame.text);
            while (matches.hasNext()) {
                frame.fieldPlaceholders.append(matches.next().captured(1));
            }
        } else if (frame.kind == ReportFrameKind::SystemText) {
            const QRegularExpression pattern(QStringLiteral("\\{([^}]+)\\}"));
            QRegularExpressionMatchIterator matches = pattern.globalMatch(frame.text);
            while (matches.hasNext()) {
                frame.systemTokens.append(matches.next().captured(1));
            }
        }
    }
    refreshFrameTable();
    refreshFrameProperties();
    markDirty();
}

void ReportDesignerWidget::setReportFont(bool dataFont, ReportFontDefinition font)
{
    ReportFontDefinition& current = dataFont ? m_report.dataFont : m_report.textFont;
    if (current == font) {
        return;
    }
    pushUndoState();
    current = std::move(font);
    markDirty();
}

void ReportDesignerWidget::save()
{
    commitPendingEdits();
    m_dirty = false;
    emit dirtyChanged(false);
    emit saveRequested(m_report);
}

void ReportDesignerWidget::closeEvent(QCloseEvent* event)
{
    if (!m_dirty) {
        event->accept();
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("CardStack Reports"),
        tr("Save changes to \"%1\" before closing?").arg(m_report.name),
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

void ReportDesignerWidget::normalizeReport()
{
    if (m_report.name.trimmed().isEmpty()) {
        m_report.name = tr("Untitled Report");
    }
    if (m_report.formatMagic.isEmpty()) {
        m_report.formatMagic = QStringLiteral("RPT@#$B");
    }
    if (m_report.formType == ReportFormType::Unknown) {
        m_report.formType = ReportFormType::Report;
    }
    if (m_report.formWidth <= 0) {
        m_report.formWidth = 8500;
    }
    if (m_report.formHeight <= 0) {
        m_report.formHeight = 11000;
    }
    if (m_report.rows <= 0) {
        m_report.rows = 1;
    }
    if (m_report.columns <= 0) {
        m_report.columns = 1;
    }
    m_report.marginLeft = std::max(0, m_report.marginLeft);
    m_report.marginTop = std::max(0, m_report.marginTop);
    m_report.marginRight = std::max(0, m_report.marginRight);
    m_report.marginBottom = std::max(0, m_report.marginBottom);
    m_report.horizontalGutter = std::max(0, m_report.horizontalGutter);
    m_report.verticalGutter = std::max(0, m_report.verticalGutter);
    if (m_report.formType == ReportFormType::Report) {
        m_report.headerHeight = std::max(0, m_report.headerHeight);
        m_report.footerHeight = std::max(0, m_report.footerHeight);
    } else {
        m_report.headerHeight = 0;
        m_report.footerHeight = 0;
    }
}

void ReportDesignerWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* top = new QHBoxLayout;
    m_nameEdit = new QLineEdit(m_report.name, this);
    m_nameEdit->setObjectName(QStringLiteral("reportDesignerName"));
    top->addWidget(new QLabel(tr("Name:"), this));
    top->addWidget(m_nameEdit, 1);
    root->addLayout(top);

    auto* splitter = new QSplitter(this);
    m_canvas = new ReportDesignCanvas(splitter);
    m_canvas->setReport(&m_report);
    splitter->addWidget(m_canvas);

    auto* side = new QWidget(splitter);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    m_frameTable = new DesignerFrameTable(side);
    m_frameTable->setObjectName(QStringLiteral("reportDesignerFrameTable"));
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

    m_propertyForm = new QFormLayout;
    m_kindCombo = new QComboBox(side);
    m_kindCombo->addItem(kindName(ReportFrameKind::Text), static_cast<int>(ReportFrameKind::Text));
    m_kindCombo->addItem(kindName(ReportFrameKind::Data), static_cast<int>(ReportFrameKind::Data));
    m_kindCombo->addItem(kindName(ReportFrameKind::SystemText), static_cast<int>(ReportFrameKind::SystemText));
    m_kindCombo->addItem(kindName(ReportFrameKind::LineOrBox), static_cast<int>(ReportFrameKind::LineOrBox));
    m_alignmentCombo = new QComboBox(side);
    m_alignmentCombo->addItem(tr("Left"), 0);
    m_alignmentCombo->addItem(tr("Center"), ReportStyleFlagAlignCenter);
    m_alignmentCombo->addItem(tr("Right"), ReportStyleFlagAlignRight);
    m_lineShapeCombo = new QComboBox(side);
    m_lineShapeCombo->addItem(tr("Box"), ReportLineShapeBox);
    m_lineShapeCombo->addItem(tr("Horizontal line"), ReportLineShapeHorizontal);
    m_lineShapeCombo->addItem(tr("Vertical line"), ReportLineShapeVertical);
    m_lineStyleCombo = new QComboBox(side);
    const QStringList lineStyles = lineStyleNames();
    for (int index = 0; index < lineStyles.size(); ++index) {
        m_lineStyleCombo->addItem(lineStyles.at(index), index);
    }
    m_fillPatternCombo = new QComboBox(side);
    const QStringList fillPatterns = fillPatternNames();
    for (int index = 0; index < fillPatterns.size(); ++index) {
        m_fillPatternCombo->addItem(fillPatterns.at(index), index);
    }
    m_textEdit = new QPlainTextEdit(side);
    m_textEdit->setObjectName(QStringLiteral("reportDesignerText"));
    m_textEdit->setMaximumHeight(90);
    m_printEntireContents = new QCheckBox(tr("Print entire contents"), side);
    m_boldCheck = new QCheckBox(tr("Bold"), side);
    m_italicCheck = new QCheckBox(tr("Italic"), side);
    m_underlineCheck = new QCheckBox(tr("Underline"), side);
    m_cornerRadiusSpin = new QSpinBox(side);
    m_leftSpin = new QSpinBox(side);
    m_topSpin = new QSpinBox(side);
    m_widthSpin = new QSpinBox(side);
    m_heightSpin = new QSpinBox(side);
    for (QSpinBox* spin : {m_cornerRadiusSpin, m_leftSpin, m_topSpin, m_widthSpin, m_heightSpin}) {
        spin->setRange(0, 20000);
    }
    m_widthSpin->setMinimum(1);
    m_heightSpin->setMinimum(1);
    m_propertyForm->addRow(tr("Kind"), m_kindCombo);
    m_propertyForm->addRow(tr("Text"), m_textEdit);
    m_propertyForm->addRow(tr("Alignment"), m_alignmentCombo);
    m_propertyForm->addRow(tr("Line shape"), m_lineShapeCombo);
    m_propertyForm->addRow(tr("Line style"), m_lineStyleCombo);
    m_propertyForm->addRow(tr("Fill pattern"), m_fillPatternCombo);
    m_propertyForm->addRow(tr("Corner radius"), m_cornerRadiusSpin);
    m_propertyForm->addRow(QString(), m_printEntireContents);
    m_propertyForm->addRow(tr("Style"), m_boldCheck);
    m_propertyForm->addRow(QString(), m_italicCheck);
    m_propertyForm->addRow(QString(), m_underlineCheck);
    m_propertyForm->addRow(tr("Left"), m_leftSpin);
    m_propertyForm->addRow(tr("Top"), m_topSpin);
    m_propertyForm->addRow(tr("Width"), m_widthSpin);
    m_propertyForm->addRow(tr("Height"), m_heightSpin);
    sideLayout->addLayout(m_propertyForm);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(m_nameEdit, &QLineEdit::textEdited, this, &ReportDesignerWidget::markDirty);
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
    m_canvas->deleteRequested = [this]() { deleteSelectedFrame(); };
    m_canvas->frameSelected = [this](int frameIndex) {
        selectFrame(frameIndex);
    };
    m_canvas->frameActivated = [this](int frameIndex) {
        selectFrame(frameIndex);
        emit commandRequested(UiIds::Command::ToolFrameAttributes);
    };
    m_canvas->framesSelected = [this](const QVector<int>& selected) { selectFrames(selected); };
    m_canvas->framesBoundsChanged = [this](const DesignerSelection::BoundsChanges& values) {
        for (const auto& value : values) {
            if (value.first < 0 || value.first >= m_report.frames.size()) continue;
            m_report.frames[value.first].bounds = value.second;
            m_report.frames[value.first].band = reportBandForBounds(m_report, value.second);
        }
        refreshFrameProperties();
        refreshFrameTable();
        markDirty();
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() { emit selectedFrameChanged(); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget*) { emit selectedFrameChanged(); });
    connect(m_textEdit, &QPlainTextEdit::selectionChanged, this, [this]() { emit selectedFrameChanged(); });
    connect(m_nameEdit, &QLineEdit::selectionChanged, this, [this]() { emit selectedFrameChanged(); });

    m_canvas->frameBoundsChangeStarted = [this](int) {
        pushUndoState();
    };
    m_canvas->frameBoundsChanged = [this](int frameIndex, const QRect& bounds) {
        if (frameIndex < 0 || frameIndex >= m_report.frames.size()) {
            return;
        }
        m_report.frames[frameIndex].bounds = bounds;
        m_report.frames[frameIndex].band = reportBandForBounds(m_report, m_report.frames[frameIndex].bounds);
        if (m_selectedFrameIndex != frameIndex) {
            selectFrame(frameIndex);
        } else {
            refreshFrameProperties();
        }
        refreshFrameTable();
        markDirty();
    };

    const auto propertyChanged = [this]() {
        applyPropertyEdits();
        refreshFrameTable();
        refreshFrameProperties();
        markDirty();
    };
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_alignmentCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_lineShapeCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_lineStyleCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_fillPatternCombo, &QComboBox::currentIndexChanged, this, propertyChanged);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, propertyChanged);
    connect(m_printEntireContents, &QCheckBox::toggled, this, propertyChanged);
    connect(m_boldCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_italicCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_underlineCheck, &QCheckBox::toggled, this, propertyChanged);
    connect(m_cornerRadiusSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_leftSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_topSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_widthSpin, &QSpinBox::valueChanged, this, propertyChanged);
    connect(m_heightSpin, &QSpinBox::valueChanged, this, propertyChanged);
}

void ReportDesignerWidget::refreshFrameTable()
{
    const QSignalBlocker blocker(m_frameTable);
    m_frameTable->setRowCount(m_report.frames.size());
    for (int row = 0; row < m_report.frames.size(); ++row) {
        const ReportFrameDefinition& frame = m_report.frames.at(row);
        m_frameTable->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_frameTable->setItem(row, 1, new QTableWidgetItem(kindName(frame.kind)));
        m_frameTable->setItem(row, 2, new QTableWidgetItem(displayTextForFrame(frame)));
    }
    syncFrameTableSelection();
    if (m_canvas != nullptr) {
        m_canvas->update();
    }
}

void ReportDesignerWidget::refreshFrameProperties()
{
    const bool group = m_selectedFrameIndices.size() > 1;
    const bool hasFrame = m_selectedFrameIndices.size() == 1 && m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report.frames.size();
    m_selectionSummary->setVisible(!hasFrame);
    if (group) m_selectionSummary->setText(tr("%1 frames selected. Drag the selection or use arrows to move the group.").arg(m_selectedFrameIndices.size()));
    else if (!hasFrame) m_selectionSummary->setText(tr("No frame selected. Click a frame or drag a rectangle to select frames."));
    for (int row = 0; row < m_propertyForm->rowCount(); ++row) m_propertyForm->setRowVisible(row, hasFrame);
    for (QWidget* widget : {static_cast<QWidget*>(m_kindCombo), static_cast<QWidget*>(m_alignmentCombo),
             static_cast<QWidget*>(m_lineShapeCombo), static_cast<QWidget*>(m_lineStyleCombo),
             static_cast<QWidget*>(m_fillPatternCombo), static_cast<QWidget*>(m_textEdit),
             static_cast<QWidget*>(m_printEntireContents),
             static_cast<QWidget*>(m_boldCheck), static_cast<QWidget*>(m_italicCheck),
             static_cast<QWidget*>(m_underlineCheck), static_cast<QWidget*>(m_cornerRadiusSpin),
             static_cast<QWidget*>(m_leftSpin),
             static_cast<QWidget*>(m_topSpin), static_cast<QWidget*>(m_widthSpin),
             static_cast<QWidget*>(m_heightSpin)}) {
        widget->setEnabled(hasFrame);
    }
    if (!hasFrame) {
        return;
    }

    const ReportFrameDefinition& frame = m_report.frames.at(m_selectedFrameIndex);
    const QSignalBlocker kindBlocker(m_kindCombo);
    const QSignalBlocker alignmentBlocker(m_alignmentCombo);
    const QSignalBlocker lineShapeBlocker(m_lineShapeCombo);
    const QSignalBlocker lineStyleBlocker(m_lineStyleCombo);
    const QSignalBlocker fillPatternBlocker(m_fillPatternCombo);
    const QSignalBlocker textBlocker(m_textEdit);
    const QSignalBlocker entireBlocker(m_printEntireContents);
    const QSignalBlocker boldBlocker(m_boldCheck);
    const QSignalBlocker italicBlocker(m_italicCheck);
    const QSignalBlocker underlineBlocker(m_underlineCheck);
    const QSignalBlocker radiusBlocker(m_cornerRadiusSpin);
    const QSignalBlocker leftBlocker(m_leftSpin);
    const QSignalBlocker topBlocker(m_topSpin);
    const QSignalBlocker widthBlocker(m_widthSpin);
    const QSignalBlocker heightBlocker(m_heightSpin);
    const int kindIndex = m_kindCombo->findData(static_cast<int>(frame.kind));
    m_kindCombo->setCurrentIndex(std::max(0, kindIndex));
    const int alignmentValue = (frame.styleFlags & ReportStyleFlagAlignRight) != 0
        ? ReportStyleFlagAlignRight
        : ((frame.styleFlags & ReportStyleFlagAlignCenter) != 0 ? ReportStyleFlagAlignCenter : 0);
    const int alignmentIndex = m_alignmentCombo->findData(alignmentValue);
    m_alignmentCombo->setCurrentIndex(std::max(0, alignmentIndex));
    const int lineShapeIndex = m_lineShapeCombo->findData(frame.lineBoxShape);
    m_lineShapeCombo->setCurrentIndex(std::max(0, lineShapeIndex));
    const int lineStyleIndex = m_lineStyleCombo->findData(frame.lineStyle);
    m_lineStyleCombo->setCurrentIndex(std::max(0, lineStyleIndex));
    const int fillPatternIndex = m_fillPatternCombo->findData(frame.fillPattern);
    m_fillPatternCombo->setCurrentIndex(std::max(0, fillPatternIndex));
    m_textEdit->setPlainText(frame.text);
    m_printEntireContents->setChecked(frame.printEntireContentsFlag != 0);
    m_boldCheck->setChecked((frame.styleFlags & ReportStyleFlagBold) != 0);
    m_italicCheck->setChecked((frame.styleFlags & ReportStyleFlagItalic) != 0);
    m_underlineCheck->setChecked((frame.styleFlags & ReportStyleFlagUnderline) != 0);
    m_cornerRadiusSpin->setValue(std::max(0, frame.cornerRadius));
    m_leftSpin->setValue(frame.bounds.left());
    m_topSpin->setValue(frame.bounds.top());
    m_widthSpin->setValue(std::max(1, frame.bounds.width()));
    m_heightSpin->setValue(std::max(1, frame.bounds.height()));

    const bool lineOrBox = frame.kind == ReportFrameKind::LineOrBox;
    const bool textLikeFrame = frame.kind == ReportFrameKind::Text
        || frame.kind == ReportFrameKind::Data
        || frame.kind == ReportFrameKind::SystemText;
    const bool dataFrame = frame.kind == ReportFrameKind::Data;
    setPropertyRowVisible(m_textEdit, textLikeFrame);
    setPropertyRowVisible(m_alignmentCombo, textLikeFrame);
    setPropertyRowVisible(m_printEntireContents, dataFrame);
    setPropertyRowVisible(m_boldCheck, textLikeFrame);
    setPropertyRowVisible(m_italicCheck, textLikeFrame);
    setPropertyRowVisible(m_underlineCheck, textLikeFrame);
    setPropertyRowVisible(m_lineShapeCombo, lineOrBox);
    setPropertyRowVisible(m_lineStyleCombo, lineOrBox);
    setPropertyRowVisible(m_fillPatternCombo, lineOrBox);
    setPropertyRowVisible(m_cornerRadiusSpin, lineOrBox);
    m_lineShapeCombo->setEnabled(lineOrBox);
    m_lineStyleCombo->setEnabled(lineOrBox);
    m_fillPatternCombo->setEnabled(lineOrBox);
    m_cornerRadiusSpin->setEnabled(lineOrBox);
}

void ReportDesignerWidget::selectFrame(int frameIndex)
{
    selectFrames(frameIndex >= 0 ? QVector<int>{frameIndex} : QVector<int>{});
}

void ReportDesignerWidget::syncFrameTableSelection()
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

void ReportDesignerWidget::selectFrames(const QVector<int>& frameIndices)
{
    const QVector<int> normalized = DesignerSelection::normalized(frameIndices, int(m_report.frames.size()));
    if (!m_restoringUndoState && normalized != m_selectedFrameIndices) applyPropertyEdits();
    m_selectedFrameIndices = normalized;
    m_selectedFrameIndex = normalized.isEmpty() ? -1 : normalized.last();
    syncFrameTableSelection();
    if (m_canvas) m_canvas->setSelectedFrames(normalized);
    refreshFrameProperties();
    emit selectedFrameChanged();
}

void ReportDesignerWidget::markDirty()
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

void ReportDesignerWidget::pushUndoState()
{
    if (m_restoringUndoState) {
        return;
    }
    constexpr int MaximumUndoStates = 100;
    if (m_undoStack.size() >= MaximumUndoStates) {
        m_undoStack.removeFirst();
    }
    m_undoStack.append({m_report, m_selectedFrameIndex, m_selectedFrameIndices, m_canvas ? m_canvas->selectionAnchor() : -1});
}

void ReportDesignerWidget::undo()
{
    if (m_undoStack.isEmpty()) {
        return;
    }
    m_restoringUndoState = true;
    const UndoState state = m_undoStack.takeLast();
    m_report = state.report;
    if (m_nameEdit != nullptr) {
        m_nameEdit->setText(m_report.name);
    }
    if (m_canvas != nullptr) {
        m_canvas->setReport(&m_report);
    }
    refreshFrameTable();
    selectFrames(state.selectedFrameIndices);
    if (m_canvas) m_canvas->setSelectionAnchor(state.selectionAnchor);
    m_restoringUndoState = false;
    markDirty();
}

void ReportDesignerWidget::applyPropertyEdits()
{
    if (m_selectedFrameIndices.size() != 1 || m_selectedFrameIndex < 0
        || m_selectedFrameIndex >= m_report.frames.size() || m_kindCombo == nullptr) return;
    const ReportFrameDefinition previousFrame = m_report.frames.at(m_selectedFrameIndex);
    pushUndoState();
    auto& frame = m_report.frames[m_selectedFrameIndex];
    frame.kind = static_cast<ReportFrameKind>(m_kindCombo->currentData().toInt());
    frame.bounds = QRect(m_leftSpin->value(), m_topSpin->value(), m_widthSpin->value(), m_heightSpin->value());
    if (frame.bounds != previousFrame.bounds) frame.band = reportBandForBounds(m_report, frame.bounds);
    const bool styled = frame.kind == ReportFrameKind::Text || frame.kind == ReportFrameKind::Data
        || frame.kind == ReportFrameKind::SystemText;
    if (styled) {
        frame.text = m_textEdit->toPlainText();
        constexpr quint8 EditableStyleMask = ReportStyleFlagUnderline | ReportStyleFlagBold | ReportStyleFlagItalic
            | ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight;
        quint8 flags = frame.styleFlags & ~EditableStyleMask;
        if (m_boldCheck->isChecked()) flags |= ReportStyleFlagBold;
        if (m_italicCheck->isChecked()) flags |= ReportStyleFlagItalic;
        if (m_underlineCheck->isChecked()) flags |= ReportStyleFlagUnderline;
        frame.styleFlags = flags | quint8(m_alignmentCombo->currentData().toInt());
    }
    if (frame.kind == ReportFrameKind::Data) frame.printEntireContentsFlag = m_printEntireContents->isChecked() ? 1 : 0;
    if (frame.kind == ReportFrameKind::LineOrBox) {
        frame.lineBoxShape = m_lineShapeCombo->currentData().toInt();
        frame.lineStyle = std::clamp(m_lineStyleCombo->currentData().toInt(), 0, ReportLineStyleCount - 1);
        frame.fillPattern = std::clamp(m_fillPatternCombo->currentData().toInt(), 0, ReportFillPatternCount - 1);
        frame.cornerRadius = std::max(0, m_cornerRadiusSpin->value());
    }
    if (frame.text != previousFrame.text || frame.kind != previousFrame.kind) {
        if (frame.kind == ReportFrameKind::Data) {
            frame.fieldPlaceholders.clear();
            const QRegularExpression pattern(QStringLiteral("\\[([^\\]]+)\\]"));
            auto matches = pattern.globalMatch(frame.text);
            while (matches.hasNext()) frame.fieldPlaceholders.append(matches.next().captured(1));
        } else if (frame.kind == ReportFrameKind::SystemText) {
            frame.systemTokens.clear();
            const QRegularExpression pattern(QStringLiteral("\\{([^}]+)\\}"));
            auto matches = pattern.globalMatch(frame.text);
            while (matches.hasNext()) frame.systemTokens.append(matches.next().captured(1));
        }
    }
    if (frame == previousFrame && !m_undoStack.isEmpty()) m_undoStack.removeLast();
}

void ReportDesignerWidget::setPropertyRowVisible(QWidget* field, bool visible)
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

ReportFrameDefinition ReportDesignerWidget::defaultFrame(ReportFrameKind kind) const
{
    ReportFrameDefinition frame;
    frame.signature = 0xabcd;
    frame.kind = kind;
    const int offset = 400 + m_report.frames.size() * 180;
    const int top = m_report.formType == ReportFormType::Report ? std::max(offset, m_report.headerHeight) : offset;
    const int width = std::min(2200, m_report.formWidth);
    const int height = std::min(kind == ReportFrameKind::LineOrBox ? 900 : 420, m_report.formHeight);
    frame.bounds = QRect(std::clamp(offset, 0, m_report.formWidth - width),
                         std::clamp(top, 0, m_report.formHeight - height), width, height);
    frame.band = reportBandForBounds(m_report, frame.bounds);
    frame.order = static_cast<quint16>(m_report.frames.size());
    switch (kind) {
    case ReportFrameKind::Data: {
        const QString field = m_fieldNames.isEmpty() ? tr("Field") : m_fieldNames.first();
        frame.text = QStringLiteral("[%1]").arg(field);
        frame.fieldPlaceholders = {field};
        break;
    }
    case ReportFrameKind::SystemText:
        frame.text = QStringLiteral("{page}");
        frame.systemTokens = {QStringLiteral("page")};
        break;
    case ReportFrameKind::LineOrBox:
        frame.text.clear();
        frame.lineBoxShape = ReportLineShapeBox;
        break;
    case ReportFrameKind::Text:
    case ReportFrameKind::Unknown:
        frame.text = tr("Report Text");
        frame.kind = ReportFrameKind::Text;
        break;
    }
    return frame;
}

} // namespace CardStack
