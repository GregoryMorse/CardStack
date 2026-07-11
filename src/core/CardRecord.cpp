#include "CardRecord.h"

#include <utility>

namespace CardStack {

namespace {
const QString emptyValue;
}

CardRecord::CardRecord(QVector<QString> values)
    : m_values(std::move(values))
{
}

int CardRecord::fieldCount() const
{
    return m_values.size();
}

const QString& CardRecord::valueAt(int index) const
{
    if (index < 0 || index >= m_values.size()) {
        return emptyValue;
    }

    return m_values[index];
}

void CardRecord::setValueAt(int index, QString value)
{
    if (index < 0) {
        return;
    }

    if (index >= m_values.size()) {
        m_values.resize(index + 1);
    }

    m_values[index] = std::move(value);
}

void CardRecord::appendValue(QString value)
{
    m_values.append(std::move(value));
}

} // namespace CardStack
