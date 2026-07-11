#pragma once

#include <QString>
#include <QVector>

namespace CardStack {

class CardRecord {
public:
    explicit CardRecord(QVector<QString> values = {});

    int fieldCount() const;
    const QString& valueAt(int index) const;
    void setValueAt(int index, QString value);
    void appendValue(QString value);

private:
    QVector<QString> m_values;
};

} // namespace CardStack

