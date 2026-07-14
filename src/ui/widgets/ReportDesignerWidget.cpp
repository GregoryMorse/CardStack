#include "ReportDesignerWidget.h"

#include "ReportPreviewRenderer.h"
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

QRectF scaledBounds(const QRect& bounds, const ReportDefinition& report, const QRectF& page)
{
    const qreal reportWidth = std::max(1, report.formWidth);
    const qreal reportHeight = std::max(1, report.formHeight);
    const qreal xScale = page.width() / reportWidth;
    const qreal yScale = page.height() / reportHeight;
    const QRectF content = page.adjusted(
        report.marginLeft * xScale,
        report.marginTop * yScale,
        -report.marginRight * xScale,
        -report.marginBottom * yScale);
    const QRectF target = content.isValid() && !content.isEmpty() ? content : page;
    return QRectF(
        target.left() + bounds.left() * target.width() / reportWidth,
        target.top() + bounds.top() * target.height() / reportHeight,
        bounds.width() * target.width() / reportWidth,
        bounds.height() * target.height() / reportHeight);
}

QRect unscaledBounds(const QRectF& bounds, const ReportDefinition& report, const QRectF& page)
{
    const qreal reportWidth = std::max(1, report.formWidth);
    const qreal reportHeight = std::max(1, report.formHeight);
    return QRect(
        qRound((bounds.left() - page.left()) * reportWidth / page.width()),
        qRound((bounds.top() - page.top()) * reportHeight / page.height()),
        qRound(bounds.width() * reportWidth / page.width()),
        qRound(bounds.height() * reportHeight / page.height()));
}

QRect normalizedFrameBounds(QRect bounds, const ReportDefinition& report)
{
    bounds.setWidth(std::max(1, bounds.width()));
    bounds.setHeight(std::max(1, bounds.height()));
    bounds.moveLeft(std::clamp(bounds.left(), 0, std::max(0, report.formWidth - bounds.width())));
    bounds.moveTop(std::clamp(bounds.top(), 0, std::max(0, report.formHeight - bounds.height())));
    return bounds;
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

class ReportDesignCanvas : public QWidget {
public:
    explicit ReportDesignCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
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
        update();
    }

    void setSelectedFrameIndex(int frameIndex)
    {
        m_selectedFrameIndex = frameIndex;
        update();
    }

    std::function<void(int)> frameSelected;
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
        drawRulers(painter, page);
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
        ReportPreviewRenderer::render(&painter, *m_report, page, previewData);

        const QColor guideBlue(QStringLiteral("#0057b8"));
        const QColor selectionRed(QStringLiteral("#c51f1f"));
        painter.setPen(QPen(guideBlue, 1, Qt::SolidLine));
        painter.drawRect(page.adjusted(0.0, 0.0, -1.0, -1.0));

        const qreal xScale = page.width() / std::max(1, m_report->formWidth);
        const qreal yScale = page.height() / std::max(1, m_report->formHeight);
        const QRectF printableArea = page.adjusted(
            m_report->marginLeft * xScale,
            m_report->marginTop * yScale,
            -m_report->marginRight * xScale,
            -m_report->marginBottom * yScale);
        if (printableArea.isValid() && !printableArea.isEmpty()) {
            painter.setPen(QPen(guideBlue, 1, Qt::DotLine));
            painter.drawRect(printableArea.adjusted(0.0, 0.0, -1.0, -1.0));
            if (m_report->formType == ReportFormType::Report) {
                const qreal headerMils = m_report->headerHeight * 1000.0 / 96.0;
                const qreal footerMils = m_report->footerHeight * 1000.0 / 96.0;
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
            const bool selected = index == m_selectedFrameIndex;
            painter.setPen(selected
                               ? QPen(selectionRed, 2, Qt::SolidLine)
                               : QPen(guideBlue, 1, Qt::DotLine));
            painter.drawRect(frameRect.adjusted(0.0, 0.0, -1.0, -1.0));
            if (selected) {
                painter.setPen(QPen(selectionRed, 1, Qt::DotLine));
                painter.drawRect(frameRect.adjusted(3.0, 3.0, -4.0, -4.0));
                painter.setBrush(selectionRed);
                painter.setPen(Qt::NoPen);
                painter.drawRect(resizeHandle(frameRect));
                painter.setBrush(Qt::NoBrush);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (m_report == nullptr) {
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_report->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_report->frames.at(index).bounds, *m_report, page);
            if (frameRect.contains(event->position())) {
                if (frameSelected) {
                    frameSelected(index);
                }
                m_dragFrameIndex = index;
                m_dragStartPosition = event->position();
                m_dragStartBounds = m_report->frames.at(index).bounds;
                m_dragMode = resizeHandle(frameRect).contains(event->position()) ? DragMode::Resize : DragMode::Move;
                m_dragChanged = false;
                return;
            }
        }
        if (frameSelected) {
            frameSelected(-1);
        }
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
        if (m_report == nullptr) {
            return;
        }

        if (m_dragMode != DragMode::None && m_dragFrameIndex >= 0) {
            if (!m_dragChanged) {
                if (frameBoundsChangeStarted) {
                    frameBoundsChangeStarted(m_dragFrameIndex);
                }
                m_dragChanged = true;
            }
            const QRectF page = pageRect();
            const QPointF delta = event->position() - m_dragStartPosition;
            QRectF visual = scaledBounds(m_dragStartBounds, *m_report, page);
            if (m_dragMode == DragMode::Move) {
                visual.translate(delta);
            } else {
                visual.setWidth(std::max<qreal>(8.0, visual.width() + delta.x()));
                visual.setHeight(std::max<qreal>(8.0, visual.height() + delta.y()));
            }

            QRect bounds = normalizedFrameBounds(unscaledBounds(visual, *m_report, page), *m_report);
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
        m_dragChanged = false;
        updateCursor(mapFromGlobal(QCursor::pos()));
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
        const QColor rulerBackground(QStringLiteral("#f5df72"));
        const QColor rulerEdge(QStringLiteral("#8f7c2b"));
        const QColor rulerText(QStringLiteral("#3f3615"));
        painter.fillRect(QRect(0, 0, width(), RulerThickness), rulerBackground);
        painter.fillRect(QRect(0, 0, RulerThickness, height()), rulerBackground);
        painter.setPen(rulerEdge);
        painter.drawLine(0, RulerThickness - 1, width(), RulerThickness - 1);
        painter.drawLine(RulerThickness - 1, 0, RulerThickness - 1, height());

        if (m_report == nullptr) {
            return;
        }

        const int formWidth = std::max(1, m_report->formWidth);
        const int formHeight = std::max(1, m_report->formHeight);
        painter.setPen(rulerText);
        for (int value = 0; value <= formWidth; value += MinorTickMils) {
            const qreal x = page.left() + page.width() * value / formWidth;
            const bool major = value % MajorTickMils == 0;
            const bool half = value % (MajorTickMils / 2) == 0;
            const int tickLength = major ? 11 : half ? 8 : 5;
            painter.drawLine(QPointF(x, RulerThickness - 1),
                             QPointF(x, RulerThickness - 1 - tickLength));
            if (major) {
                painter.drawText(QRectF(x + 2, 1, 30, 13),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(value / MajorTickMils));
            }
        }
        if (formWidth % MajorTickMils != 0) {
            painter.drawText(
                QRectF(page.right() - 38, 1, 36, 13),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(formWidth / static_cast<double>(MajorTickMils), 'g', 4));
        }
        for (int value = 0; value <= formHeight; value += MinorTickMils) {
            const qreal y = page.top() + page.height() * value / formHeight;
            const bool major = value % MajorTickMils == 0;
            const bool half = value % (MajorTickMils / 2) == 0;
            const int tickLength = major ? 11 : half ? 8 : 5;
            painter.drawLine(QPointF(RulerThickness - 1, y),
                             QPointF(RulerThickness - 1 - tickLength, y));
            if (major) {
                painter.drawText(QRectF(1, y + 2, 17, 13),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(value / MajorTickMils));
            }
        }
        if (formHeight % MajorTickMils != 0) {
            painter.drawText(
                QRectF(1, page.bottom() - 14, 22, 13),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(formHeight / static_cast<double>(MajorTickMils), 'g', 4));
        }

        painter.setPen(QPen(rulerEdge, 2));
        painter.drawLine(QPointF(page.left(), RulerThickness - 2),
                         QPointF(page.right(), RulerThickness - 2));
        painter.drawLine(QPointF(RulerThickness - 2, page.top()),
                         QPointF(RulerThickness - 2, page.bottom()));
    }

    QRectF pageRect() const
    {
        if (m_report == nullptr) {
            return rect().adjusted(12, 12, -12, -12);
        }

        const qreal reportWidth = std::max(1, m_report->formWidth);
        const qreal reportHeight = std::max(1, m_report->formHeight);
        const qreal aspect = reportWidth / reportHeight;
        QRectF available = rect().adjusted(
            RulerThickness + 16,
            RulerThickness + 16,
            -20,
            -20);
        qreal pageWidth = available.width();
        qreal pageHeight = pageWidth / aspect;
        if (pageHeight > available.height()) {
            pageHeight = available.height();
            pageWidth = pageHeight * aspect;
        }
        return QRectF(
            available.center().x() - pageWidth / 2.0,
            available.center().y() - pageHeight / 2.0,
            pageWidth,
            pageHeight);
    }

    QRectF resizeHandle(const QRectF& frameRect) const
    {
        return QRectF(frameRect.right() - 7.0, frameRect.bottom() - 7.0, 7.0, 7.0);
    }

    void updateCursor(const QPointF& position)
    {
        if (m_report == nullptr) {
            unsetCursor();
            return;
        }

        const QRectF page = pageRect();
        for (int index = m_report->frames.size() - 1; index >= 0; --index) {
            const QRectF frameRect = scaledBounds(m_report->frames.at(index).bounds, *m_report, page);
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

    const ReportDefinition* m_report = nullptr;
    int m_selectedFrameIndex = -1;
    int m_dragFrameIndex = -1;
    DragMode m_dragMode = DragMode::None;
    bool m_dragChanged = false;
    QPointF m_dragStartPosition;
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
    return m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report.frames.size()
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
    int footerHeight)
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
    m_report.headerHeight = m_report.formType == ReportFormType::Report
        ? std::max(0, headerHeight)
        : 0;
    m_report.footerHeight = m_report.formType == ReportFormType::Report
        ? std::max(0, footerHeight)
        : 0;
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
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_report.frames.size()) {
        return;
    }

    pushUndoState();
    m_report.frames.removeAt(m_selectedFrameIndex);
    refreshFrameTable();
    selectFrame(std::min(m_selectedFrameIndex, static_cast<int>(m_report.frames.size()) - 1));
    markDirty();
}

void ReportDesignerWidget::copySelectedFrame()
{
    commitPendingEdits();
    if (m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report.frames.size()) {
        m_copiedFrame = m_report.frames.at(m_selectedFrameIndex);
    }
}

void ReportDesignerWidget::cutSelectedFrame()
{
    copySelectedFrame();
    if (m_copiedFrame.has_value()) {
        deleteSelectedFrame();
    }
}

void ReportDesignerWidget::pasteFrame()
{
    if (!m_copiedFrame.has_value()) {
        return;
    }
    pushUndoState();
    ReportFrameDefinition frame = *m_copiedFrame;
    frame.bounds.translate(120, 120);
    m_report.frames.append(std::move(frame));
    refreshFrameTable();
    selectFrame(m_report.frames.size() - 1);
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
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_report.frames.size()) {
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
    if (current.faceName == font.faceName && current.legacyHeight == font.legacyHeight) {
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
        m_report.headerHeight = m_report.headerHeight > 0 ? m_report.headerHeight : 72;
        m_report.footerHeight = m_report.footerHeight > 0 ? m_report.footerHeight : 72;
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
    m_frameTable = new QTableWidget(side);
    m_frameTable->setObjectName(QStringLiteral("reportDesignerFrameTable"));
    m_frameTable->setColumnCount(3);
    m_frameTable->setHorizontalHeaderLabels({tr("#"), tr("Kind"), tr("Text")});
    m_frameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setSelectionMode(QAbstractItemView::SingleSelection);
    sideLayout->addWidget(m_frameTable, 1);

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
    connect(m_frameTable, &QTableWidget::currentCellChanged, this, [this](int row) {
        selectFrame(row);
    });
    m_canvas->frameSelected = [this](int frameIndex) {
        selectFrame(frameIndex);
    };
    m_canvas->frameActivated = [this](int frameIndex) {
        selectFrame(frameIndex);
        emit commandRequested(UiIds::Command::ToolFrameAttributes);
    };
    m_canvas->frameBoundsChangeStarted = [this](int) {
        pushUndoState();
    };
    m_canvas->frameBoundsChanged = [this](int frameIndex, const QRect& bounds) {
        if (frameIndex < 0 || frameIndex >= m_report.frames.size()) {
            return;
        }
        m_report.frames[frameIndex].bounds = bounds;
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
    if (m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_frameTable->rowCount()) {
        m_frameTable->setCurrentCell(m_selectedFrameIndex, 0);
    }
    if (m_canvas != nullptr) {
        m_canvas->update();
    }
}

void ReportDesignerWidget::refreshFrameProperties()
{
    const bool hasFrame = m_selectedFrameIndex >= 0 && m_selectedFrameIndex < m_report.frames.size();
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
    const int normalizedFrameIndex = frameIndex >= 0 && frameIndex < m_report.frames.size() ? frameIndex : -1;
    if (normalizedFrameIndex != m_selectedFrameIndex) {
        applyPropertyEdits();
    }
    m_selectedFrameIndex = normalizedFrameIndex;
    if (m_frameTable != nullptr && m_selectedFrameIndex >= 0 && m_frameTable->currentRow() != m_selectedFrameIndex) {
        const QSignalBlocker blocker(m_frameTable);
        m_frameTable->setCurrentCell(m_selectedFrameIndex, 0);
    }
    if (m_canvas != nullptr) {
        m_canvas->setSelectedFrameIndex(m_selectedFrameIndex);
    }
    refreshFrameProperties();
    emit selectedFrameChanged();
}

void ReportDesignerWidget::markDirty()
{
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
    m_undoStack.append({m_report, m_selectedFrameIndex});
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
    selectFrame(std::clamp(state.selectedFrameIndex, -1, static_cast<int>(m_report.frames.size()) - 1));
    m_restoringUndoState = false;
    markDirty();
}

void ReportDesignerWidget::applyPropertyEdits()
{
    if (m_selectedFrameIndex < 0 || m_selectedFrameIndex >= m_report.frames.size() || m_kindCombo == nullptr) {
        return;
    }

    const ReportFrameDefinition previousFrame = m_report.frames.at(m_selectedFrameIndex);
    pushUndoState();
    ReportFrameDefinition& frame = m_report.frames[m_selectedFrameIndex];
    frame.kind = static_cast<ReportFrameKind>(m_kindCombo->currentData().toInt());
    frame.text = m_textEdit->toPlainText();
    frame.bounds = QRect(m_leftSpin->value(), m_topSpin->value(), m_widthSpin->value(), m_heightSpin->value());
    frame.printEntireContentsFlag = m_printEntireContents->isChecked() ? 1 : 0;
    quint8 styleFlags = 0;
    if (m_boldCheck->isChecked()) {
        styleFlags |= ReportStyleFlagBold;
    }
    if (m_italicCheck->isChecked()) {
        styleFlags |= ReportStyleFlagItalic;
    }
    if (m_underlineCheck->isChecked()) {
        styleFlags |= ReportStyleFlagUnderline;
    }
    styleFlags |= static_cast<quint8>(m_alignmentCombo->currentData().toInt());
    frame.styleFlags = styleFlags;
    frame.lineBoxShape = m_lineShapeCombo->currentData().toInt();
    frame.lineStyle = std::clamp(m_lineStyleCombo->currentData().toInt(), 0, ReportLineStyleCount - 1);
    frame.fillPattern = std::clamp(m_fillPatternCombo->currentData().toInt(), 0, ReportFillPatternCount - 1);
    frame.cornerRadius = std::max(0, m_cornerRadiusSpin->value());
    frame.fieldPlaceholders.clear();
    frame.systemTokens.clear();
    if (frame.kind == ReportFrameKind::Data) {
        const QRegularExpression fieldPattern(QStringLiteral("\\[([^\\]]+)\\]"));
        QRegularExpressionMatchIterator matches = fieldPattern.globalMatch(frame.text);
        while (matches.hasNext()) {
            frame.fieldPlaceholders.append(matches.next().captured(1));
        }
    } else if (frame.kind == ReportFrameKind::SystemText) {
        const QRegularExpression tokenPattern(QStringLiteral("\\{([^}]+)\\}"));
        QRegularExpressionMatchIterator matches = tokenPattern.globalMatch(frame.text);
        while (matches.hasNext()) {
            frame.systemTokens.append(matches.next().captured(1));
        }
    }
    if (frame == previousFrame && !m_undoStack.isEmpty()) {
        m_undoStack.removeLast();
    }
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
    frame.bounds = QRect(offset, offset, 2200, kind == ReportFrameKind::LineOrBox ? 900 : 420);
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
