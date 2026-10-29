#pragma once

#include <QMargins>
#include <QRect>
#include <QSize>
#include <algorithm>

namespace CardStack {
inline constexpr int MaximumFormGridCount = 100;

struct ReportFormGeometry {
    QSize page;
    QMargins margins;
    QSize gutters;
    int rows = 1;
    int columns = 1;
    int formWidth = 0;
    int formHeight = 0;

    bool valid() const { return page.width() > 0 && page.height() > 0 && formWidth > 0 && formHeight > 0; }
    QRect cell(int row, int column) const
    {
        if (!valid() || row < 0 || row >= rows || column < 0 || column >= columns) return {};
        return QRect(margins.left() + column * (formWidth + gutters.width()),
                     margins.top() + row * (formHeight + gutters.height()), formWidth, formHeight);
    }
};

inline ReportFormGeometry reportFormGeometry(QSize page, QMargins margins, QSize gutters,
                                             int rows, int columns)
{
    ReportFormGeometry result;
    result.page = page;
    result.margins = QMargins(std::max(0, margins.left()), std::max(0, margins.top()),
                              std::max(0, margins.right()), std::max(0, margins.bottom()));
    result.gutters = QSize(std::max(0, gutters.width()), std::max(0, gutters.height()));
    result.rows = std::clamp(rows, 1, MaximumFormGridCount);
    result.columns = std::clamp(columns, 1, MaximumFormGridCount);
    const qint64 width = qint64(page.width()) - result.margins.left() - result.margins.right()
        - qint64(result.columns - 1) * result.gutters.width();
    const qint64 height = qint64(page.height()) - result.margins.top() - result.margins.bottom()
        - qint64(result.rows - 1) * result.gutters.height();
    result.formWidth = int(std::max<qint64>(0, width / result.columns));
    result.formHeight = int(std::max<qint64>(0, height / result.rows));
    return result;
}
} // namespace CardStack
