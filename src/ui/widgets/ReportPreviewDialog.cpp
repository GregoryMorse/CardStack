#include <QVariant>
#include "ReportPreviewDialog.h"

#include "UiIds.h"

#include <QAbstractButton>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <utility>

namespace CardStack {
namespace {

namespace Control = UiIds::Control;

class ReportPreviewCanvas : public QWidget {
public:
    ReportPreviewCanvas(
        ReportDefinition report,
        QVector<ReportPreviewData> records,
        QVector<ReportPrintPage> pages,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , m_report(std::move(report))
        , m_records(std::move(records))
        , m_pages(std::move(pages))
    {
        setObjectName(QStringLiteral("reportPreviewCanvas"));
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void(int)> pageStepRequested;

    void setCurrentPageIndex(int pageIndex)
    {
        const int lastPageIndex = std::max(0, static_cast<int>(m_pages.size()) - 1);
        m_currentPageIndex = std::clamp(pageIndex, 0, lastPageIndex);
        update();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        int pageStep = 0;
        if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Down) {
            pageStep = 1;
        } else if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Up) {
            pageStep = -1;
        }
        if (pageStep != 0 && pageStepRequested) {
            pageStepRequested(pageStep);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const int pageStep = event->angleDelta().y() < 0 ? 1 : (event->angleDelta().y() > 0 ? -1 : 0);
        if (pageStep != 0 && pageStepRequested) {
            pageStepRequested(pageStep);
            event->accept();
            return;
        }
        QWidget::wheelEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().window());

        QRectF page = pageRect();
        painter.fillRect(page.translated(4.0, 4.0), QColor(0, 0, 0, 40));
        if (!m_pages.isEmpty()) {
            ReportPrintEngine::renderPage(&painter, m_report, m_records, m_pages.at(m_currentPageIndex), page);
        } else {
            painter.fillRect(page, Qt::white);
        }
        painter.setPen(palette().shadow().color());
        painter.drawRect(page.adjusted(0.0, 0.0, -1.0, -1.0));
    }

private:
    QRectF pageRect() const
    {
        const qreal reportWidth = ReportPrintEngine::pageSizeMils(m_report).width();
        const qreal reportHeight = ReportPrintEngine::pageSizeMils(m_report).height();
        const qreal reportAspect = reportWidth / reportHeight;

        QRectF available = rect().adjusted(6, 6, -10, -10);
        if (available.isEmpty()) {
            return {};
        }

        qreal pageWidth = available.width();
        qreal pageHeight = pageWidth / reportAspect;
        if (pageHeight > available.height()) {
            pageHeight = available.height();
            pageWidth = pageHeight * reportAspect;
        }

        return QRectF(
            available.center().x() - pageWidth / 2.0,
            available.center().y() - pageHeight / 2.0,
            pageWidth,
            pageHeight);
    }

    ReportDefinition m_report;
    QVector<ReportPreviewData> m_records;
    QVector<ReportPrintPage> m_pages;
    int m_currentPageIndex = 0;
};

template <typename T>
T* uiControl(QWidget* parent, int controlId)
{
    return qobject_cast<T*>(UiBuilder::controlById(parent, controlId));
}

void setLabelText(QWidget* parent, int controlId, const QString& text)
{
    if (auto* label = uiControl<QLabel>(parent, controlId)) {
        label->setText(text);
    }
}

} // namespace

std::unique_ptr<QDialog> ReportPreviewDialog::create(
    QWidget* parent,
    const UiBuilder::DialogContext& context,
    const ReportDefinition& report,
    const QVector<ReportPreviewData>& records,
    const QVector<ReportPrintPage>& pages)
{
    std::unique_ptr<QDialog> dialog = UiBuilder::createDialog(QStringLiteral("PRINTPREVIEW"), parent, context);
    if (!dialog) {
        return {};
    }

    dialog->setProperty("previewRecordCount", records.size());
    QVariantList rows;
    for (const auto& record : records) {
        QVariantMap row;
        for (auto it = record.fieldValues.cbegin(); it != record.fieldValues.cend(); ++it) row.insert(it.key(), it.value());
        rows.append(row);
    }
    dialog->setProperty("previewFieldRows", rows);
    auto currentPageIndex = std::make_shared<int>(0);
    const QVector<ReportPrintPage> safePages = pages.isEmpty() ? ReportPrintEngine::paginate(report, records) : pages;

    ReportPreviewCanvas* canvas = nullptr;
    QWidget* placeholder = UiBuilder::controlById(dialog.get(), Control::PreviewCanvas);
    if (placeholder != nullptr) {
        canvas = new ReportPreviewCanvas(report, records, safePages, placeholder->parentWidget());
        canvas->setGeometry(placeholder->geometry());
        canvas->show();
        placeholder->hide();
    }

    setLabelText(dialog.get(), Control::PreviewTitle, report.name);
    const int pageCount = static_cast<int>(safePages.size());
    auto updatePageState = [dialog = dialog.get(), canvas, currentPageIndex, pageCount]() {
        const int safePageIndex = std::clamp(*currentPageIndex, 0, std::max(0, pageCount - 1));
        *currentPageIndex = safePageIndex;
        if (canvas != nullptr) {
            canvas->setCurrentPageIndex(safePageIndex);
        }
        setLabelText(
            dialog,
            Control::PreviewPageStatus,
            pageCount <= 0
                ? QObject::tr("No pages")
                : QObject::tr("Page %1 of %2").arg(safePageIndex + 1).arg(pageCount));
        if (auto* firstPage = uiControl<QAbstractButton>(dialog, Control::PreviewFirstPage)) {
            firstPage->setEnabled(safePageIndex > 0);
        }
        if (auto* nextPage = uiControl<QAbstractButton>(dialog, Control::PreviewNextPage)) {
            nextPage->setEnabled(safePageIndex + 1 < pageCount);
        }
    };
    if (canvas != nullptr) {
        canvas->pageStepRequested = [currentPageIndex, updatePageState](int pageStep) {
            *currentPageIndex += pageStep;
            updatePageState();
        };
    }

    if (auto* firstPage = uiControl<QAbstractButton>(dialog.get(), Control::PreviewFirstPage)) {
        QObject::connect(firstPage, &QAbstractButton::clicked, dialog.get(), [currentPageIndex, updatePageState]() {
            *currentPageIndex = 0;
            updatePageState();
        });
    }
    if (auto* nextPage = uiControl<QAbstractButton>(dialog.get(), Control::PreviewNextPage)) {
        QObject::connect(nextPage, &QAbstractButton::clicked, dialog.get(), [currentPageIndex, updatePageState]() {
            ++(*currentPageIndex);
            updatePageState();
        });
    }
    updatePageState();

    if (canvas != nullptr) {
        auto* layout = new QGridLayout(dialog.get());
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setHorizontalSpacing(12);
        layout->setVerticalSpacing(10);
        if (auto* status = uiControl<QLabel>(dialog.get(), Control::PreviewPageStatus))
            layout->addWidget(status, 0, 0);
        if (auto* title = uiControl<QLabel>(dialog.get(), Control::PreviewTitle)) {
            title->setWordWrap(true);
            layout->addWidget(title, 0, 1);
        }
        canvas->setMinimumSize(320, 320);
        canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget(canvas, 1, 0, 1, 2);
        auto* buttons = new QVBoxLayout;
        for (int id : {Control::Ok, Control::Cancel}) {
            if (auto* button = uiControl<QAbstractButton>(dialog.get(), id)) {
                button->setMinimumWidth(120);
                buttons->addWidget(button);
            }
        }
        buttons->addSpacing(16);
        for (int id : {Control::PreviewNextPage, Control::PreviewFirstPage})
            if (auto* button = uiControl<QAbstractButton>(dialog.get(), id)) buttons->addWidget(button);
        buttons->addStretch(1);
        layout->addLayout(buttons, 1, 2);
        layout->setColumnStretch(1, 1);
        layout->setRowStretch(1, 1);
        dialog->setMinimumSize(640, 480);
        dialog->resize(960, 720);
        dialog->setSizeGripEnabled(true);
        layout->activate();
    }
    return dialog;
}

int ReportPreviewDialog::exec(
    QWidget* parent,
    const UiBuilder::DialogContext& context,
    const ReportDefinition& report,
    const QVector<ReportPreviewData>& records,
    const QVector<ReportPrintPage>& pages)
{
    std::unique_ptr<QDialog> dialog = create(parent, context, report, records, pages);
    if (!dialog) {
        return QDialog::Rejected;
    }

    return dialog->exec();
}

std::unique_ptr<QDialog> ReportPreviewDialog::create(
    QWidget* parent,
    const UiBuilder::DialogContext& context,
    const ReportDefinition& report,
    const ReportPreviewData& data)
{
    QVector<ReportPreviewData> records = {data};
    return create(parent, context, report, records, ReportPrintEngine::paginate(report, records));
}

int ReportPreviewDialog::exec(
    QWidget* parent,
    const UiBuilder::DialogContext& context,
    const ReportDefinition& report,
    const ReportPreviewData& data)
{
    QVector<ReportPreviewData> records = {data};
    return exec(parent, context, report, records, ReportPrintEngine::paginate(report, records));
}

} // namespace CardStack
