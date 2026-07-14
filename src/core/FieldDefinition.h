#pragma once

#include <QByteArray>
#include <QString>

namespace CardStack {

enum class FieldType {
    Text,
    Notes
};

class FieldDefinition {
public:
    FieldDefinition() = default;
    FieldDefinition(QString name,
                    FieldType type,
                    int maxLength,
                    bool showName = true,
                    bool phone = false,
                    QByteArray legacyDescriptor = {},
                    int displayWidth = 0);

    const QString& name() const;
    FieldType type() const;
    int maxLength() const;
    bool showName() const;
    bool isPhone() const;
    const QByteArray& legacyDescriptor() const;
    int displayWidth() const;
    void setDisplayWidth(int width);

    bool isNotes() const;

private:
    QString m_name;
    FieldType m_type = FieldType::Text;
    int m_maxLength = 255;
    bool m_showName = true;
    bool m_phone = false;
    QByteArray m_legacyDescriptor;
    int m_displayWidth = 0;
};

} // namespace CardStack
