#pragma once

#include "CardTemplateLayout.h"
#include "FieldDefinition.h"
#include "ReportDefinition.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <utility>

namespace CardStack::DesignerClipboard {

inline constexpr int FormatVersion = 1;
inline constexpr auto ReportMime = "application/x-cardstack-report-frames+json";
inline constexpr auto TemplateMime = "application/x-cardstack-template-frames+json";

enum class TextCommand { Copy, Cut, Paste, Clear };

inline QWidget* focusedEditor(const QWidget* owner)
{
    QWidget* focused = QApplication::focusWidget();
    if (!focused || !owner) return nullptr;
    const bool belongsToDesigner = owner->isAncestorOf(focused)
        || (focused->window() == owner->window()
            && focused->objectName().startsWith(QStringLiteral("designer")));
    if (!belongsToDesigner) return nullptr;
    return qobject_cast<QLineEdit*>(focused) || qobject_cast<QPlainTextEdit*>(focused)
        || qobject_cast<QTextEdit*>(focused) ? focused : nullptr;
}

inline bool textCommand(const QWidget* owner, TextCommand command)
{
    QWidget* focused = focusedEditor(owner);
    if (auto* edit = qobject_cast<QLineEdit*>(focused)) {
        if (command == TextCommand::Copy) edit->copy();
        else if (command == TextCommand::Cut) edit->cut();
        else if (command == TextCommand::Clear) { if (edit->hasSelectedText()) edit->del(); }
        else edit->paste();
        return true;
    }
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) {
        if (command == TextCommand::Copy) edit->copy();
        else if (command == TextCommand::Cut) edit->cut();
        else if (command == TextCommand::Clear) {
            QTextCursor cursor = edit->textCursor();
            cursor.removeSelectedText();
            edit->setTextCursor(cursor);
        } else edit->paste();
        return true;
    }
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) {
        if (command == TextCommand::Copy) edit->copy();
        else if (command == TextCommand::Cut) edit->cut();
        else if (command == TextCommand::Clear) {
            QTextCursor cursor = edit->textCursor();
            cursor.removeSelectedText();
            edit->setTextCursor(cursor);
        } else edit->paste();
        return true;
    }
    return false;
}

inline bool textSelection(const QWidget* owner)
{
    QWidget* focused = focusedEditor(owner);
    if (auto* edit = qobject_cast<QLineEdit*>(focused)) return edit->hasSelectedText();
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focused)) return edit->textCursor().hasSelection();
    if (auto* edit = qobject_cast<QTextEdit*>(focused)) return edit->textCursor().hasSelection();
    return false;
}

inline QJsonArray rect(const QRect& value)
{
    return {value.x(), value.y(), value.width(), value.height()};
}

inline QRect rect(const QJsonValue& value)
{
    const QJsonArray values = value.toArray();
    if (values.size() != 4) return {};
    return QRect(values.at(0).toInt(), values.at(1).toInt(), values.at(2).toInt(), values.at(3).toInt());
}

inline QJsonArray strings(const QVector<QString>& values)
{
    QJsonArray result;
    for (const QString& value : values) result.append(value);
    return result;
}

inline QVector<QString> strings(const QJsonValue& value)
{
    QVector<QString> result;
    for (const QJsonValue& entry : value.toArray()) result.append(entry.toString());
    return result;
}

inline bool write(const char* format, const QJsonObject& document)
{
    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) return false;
    const QByteArray data = QJsonDocument(document).toJson(QJsonDocument::Compact);
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(format), data);
    clipboard->setMimeData(mime);
    const QMimeData* actual = clipboard->mimeData();
    return actual && actual->data(QString::fromLatin1(format)) == data;
}

inline QJsonObject read(const char* format)
{
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mime = clipboard ? clipboard->mimeData() : nullptr;
    if (!mime || !mime->hasFormat(QString::fromLatin1(format))) return {};
    const QJsonObject document = QJsonDocument::fromJson(mime->data(QString::fromLatin1(format))).object();
    return document.value(QStringLiteral("version")).toInt() == FormatVersion ? document : QJsonObject{};
}

inline bool writeReport(const QVector<ReportFrameDefinition>& frames)
{
    if (frames.isEmpty()) return false;
    QJsonArray values;
    for (const auto& frame : frames) {
        values.append(QJsonObject{
            {"offset", frame.legacyOffset}, {"signature", int(frame.signature)}, {"sourceId", int(frame.sourceId)},
            {"order", int(frame.order)}, {"band", int(frame.band)}, {"bounds", rect(frame.bounds)},
            {"text", frame.text}, {"fields", strings(frame.fieldPlaceholders)}, {"tokens", strings(frame.systemTokens)},
            {"kind", int(frame.kind)}, {"printEntire", int(frame.printEntireContentsFlag)},
            {"validation", int(frame.validationFlags)}, {"style", int(frame.styleFlags)},
            {"shape", frame.lineBoxShape}, {"line", frame.lineStyle}, {"fill", frame.fillPattern},
            {"radius", frame.cornerRadius}, {"sourceDescriptor", QString::fromLatin1(frame.legacyDescriptor.toBase64())},
        });
    }
    return write(ReportMime, {{"version", FormatVersion}, {"frames", values}});
}

inline QVector<ReportFrameDefinition> readReport()
{
    QVector<ReportFrameDefinition> result;
    for (const QJsonValue& value : read(ReportMime).value(QStringLiteral("frames")).toArray()) {
        const QJsonObject object = value.toObject();
        ReportFrameDefinition frame;
        frame.bounds = rect(object.value(QStringLiteral("bounds")));
        const int kind = object.value(QStringLiteral("kind")).toInt(-1);
        if (!frame.bounds.isValid() || kind < int(ReportFrameKind::Unknown) || kind > int(ReportFrameKind::LineOrBox)) return {};
        frame.legacyOffset = object.value("offset").toInt();
        frame.signature = quint16(object.value("signature").toInt());
        frame.sourceId = quint16(object.value("sourceId").toInt());
        frame.order = quint16(object.value("order").toInt());
        frame.band = quint16(object.value("band").toInt());
        frame.text = object.value("text").toString();
        frame.fieldPlaceholders = strings(object.value("fields"));
        frame.systemTokens = strings(object.value("tokens"));
        frame.kind = ReportFrameKind(kind);
        frame.printEntireContentsFlag = quint8(object.value("printEntire").toInt());
        frame.validationFlags = quint8(object.value("validation").toInt());
        frame.styleFlags = quint8(object.value("style").toInt());
        frame.lineBoxShape = object.value("shape").toInt();
        frame.lineStyle = object.value("line").toInt();
        frame.fillPattern = object.value("fill").toInt();
        frame.cornerRadius = object.value("radius").toInt();
        frame.legacyDescriptor = QByteArray::fromBase64(object.value("sourceDescriptor").toString().toLatin1());
        result.append(std::move(frame));
    }
    return result;
}

struct TemplateFrames {
    QVector<CardTemplateFrame> frames;
    QMap<int, FieldDefinition> fields;
};

inline bool writeTemplate(const QVector<CardTemplateFrame>& frames, const QVector<FieldDefinition>& fields)
{
    if (frames.isEmpty()) return false;
    QJsonArray values;
    QJsonArray definitions;
    QMap<int, bool> copiedFields;
    for (const auto& frame : frames) {
        values.append(QJsonObject{
            {"kind", int(frame.kind)}, {"bounds", rect(frame.bounds)}, {"text", frame.text},
            {"field", frame.fieldIndex}, {"style", int(frame.styleFlags)}, {"shape", int(frame.lineBoxShape)},
            {"line", frame.lineStyle}, {"fill", frame.fillPattern}, {"radius", frame.cornerRadius},
            {"sourceDescriptor", QString::fromLatin1(frame.legacyDescriptor.toBase64())},
        });
        if (frame.fieldIndex >= 0 && frame.fieldIndex < fields.size() && !copiedFields.contains(frame.fieldIndex)) {
            const auto& field = fields.at(frame.fieldIndex);
            definitions.append(QJsonObject{
                {"index", frame.fieldIndex}, {"name", field.name()}, {"type", int(field.type())},
                {"length", field.maxLength()}, {"showName", field.showName()}, {"phone", field.isPhone()},
                {"displayWidth", field.displayWidth()},
                {"sourceDescriptor", QString::fromLatin1(field.legacyDescriptor().toBase64())},
            });
            copiedFields.insert(frame.fieldIndex, true);
        }
    }
    return write(TemplateMime, {{"version", FormatVersion}, {"frames", values}, {"fields", definitions}});
}

inline TemplateFrames readTemplate()
{
    TemplateFrames result;
    const QJsonObject document = read(TemplateMime);
    for (const QJsonValue& value : document.value("fields").toArray()) {
        const QJsonObject object = value.toObject();
        const int index = object.value("index").toInt(-1);
        const int type = object.value("type").toInt(-1);
        if (index < 0 || type < int(FieldType::Text) || type > int(FieldType::Notes)
            || object.value("length").toInt() <= 0 || object.value("name").toString().trimmed().isEmpty()) return {};
        result.fields.insert(index, FieldDefinition(object.value("name").toString(), FieldType(type),
            object.value("length").toInt(), object.value("showName").toBool(), object.value("phone").toBool(),
            QByteArray::fromBase64(object.value("sourceDescriptor").toString().toLatin1()),
            object.value("displayWidth").toInt()));
    }
    for (const QJsonValue& value : document.value("frames").toArray()) {
        const QJsonObject object = value.toObject();
        CardTemplateFrame frame;
        frame.bounds = rect(object.value("bounds"));
        const int kind = object.value("kind").toInt(-1);
        const int shape = object.value("shape").toInt(-1);
        if (!frame.bounds.isValid() || kind < int(CardTemplateFrameKind::Text) || kind > int(CardTemplateFrameKind::LineOrBox)
            || shape < int(CardTemplateLineBoxShape::Box) || shape > int(CardTemplateLineBoxShape::VerticalLine)) return {};
        frame.kind = CardTemplateFrameKind(kind);
        frame.text = object.value("text").toString();
        frame.fieldIndex = object.value("field").toInt(-1);
        if ((frame.kind == CardTemplateFrameKind::DataBox || frame.kind == CardTemplateFrameKind::NotesBox)
            && frame.fieldIndex >= 0 && !result.fields.contains(frame.fieldIndex)) return {};
        frame.styleFlags = quint8(object.value("style").toInt());
        frame.lineBoxShape = CardTemplateLineBoxShape(shape);
        frame.lineStyle = object.value("line").toInt();
        frame.fillPattern = object.value("fill").toInt();
        frame.cornerRadius = object.value("radius").toInt();
        frame.legacyDescriptor = QByteArray::fromBase64(object.value("sourceDescriptor").toString().toLatin1());
        result.frames.append(std::move(frame));
    }
    return result;
}

} // namespace CardStack::DesignerClipboard
