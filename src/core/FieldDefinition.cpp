#include "FieldDefinition.h"

#include <utility>

namespace CardStack {

FieldDefinition::FieldDefinition(QString name, FieldType type, int maxLength)
    : m_name(std::move(name))
    , m_type(type)
    , m_maxLength(maxLength)
{
}

const QString& FieldDefinition::name() const
{
    return m_name;
}

FieldType FieldDefinition::type() const
{
    return m_type;
}

int FieldDefinition::maxLength() const
{
    return m_maxLength;
}

bool FieldDefinition::isNotes() const
{
    return m_type == FieldType::Notes;
}

} // namespace CardStack
