#pragma once

#include <QString>

namespace CardStack {

enum class FieldType {
    Text,
    Notes
};

class FieldDefinition {
public:
    FieldDefinition() = default;
    FieldDefinition(QString name, FieldType type, int maxLength);

    const QString& name() const;
    FieldType type() const;
    int maxLength() const;

    bool isNotes() const;

private:
    QString m_name;
    FieldType m_type = FieldType::Text;
    int m_maxLength = 255;
};

} // namespace CardStack

