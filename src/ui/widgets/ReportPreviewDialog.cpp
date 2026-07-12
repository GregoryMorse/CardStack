#include "ReportPreviewDialog.h"

#include "UiIds.h"

#include <QAbstractButton>
#include <QLabel>
#include <QPainter>
#include <QWidget>

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
    }

    void setCurrentPageIndex(int pageIndex)
    {
        const int lastPageIndex = std::max(0, static_cast<int>(m_pages.size()) - 1);
        m_currentPageIndex = std::clamp(pageIndex, 0, lastPageIndex);
        update();
    }

protected:
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
        const qreal reportWidth = std::max(1, m_report.formWidth);
        const qreal reportHeight = std::max(1, m_report.formHeight);
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
