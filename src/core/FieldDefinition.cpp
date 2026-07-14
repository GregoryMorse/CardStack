#include "FieldDefinition.h"

#include <utility>

namespace CardStack {

FieldDefinition::FieldDefinition(QString name,
                                 FieldType type,
                                 int maxLength,
                                 bool showName,
                                 bool phone,
                                 QByteArray legacyDescriptor,
                                 int displayWidth)
    : m_name(std::move(name))
    , m_type(type)
    , m_maxLength(maxLength)
    , m_showName(showName)
    , m_phone(phone)
    , m_legacyDescriptor(std::move(legacyDescriptor))
    , m_displayWidth(displayWidth < 0 ? 0 : displayWidth)
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

bool FieldDefinition::showName() const
{
    return m_showName;
}

bool FieldDefinition::isPhone() const
{
    return m_phone;
}

const QByteArray& FieldDefinition::legacyDescriptor() const
{
    return m_legacyDescriptor;
}

int FieldDefinition::displayWidth() const
{
    return m_displayWidth;
}

void FieldDefinition::setDisplayWidth(int width)
{
    m_displayWidth = width < 0 ? 0 : width;
}

bool FieldDefinition::isNotes() const
{
    return m_type == FieldType::Notes;
}

} // namespace CardStack
