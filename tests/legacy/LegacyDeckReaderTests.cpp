#include "LegacyDeckReader.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "LegacyInterchangeReader.h"
#include "LegacyReportReader.h"
#include "DeckTemplate.h"
#include "LegacyTemplateLayoutParser.h"
#include <QFont>
#include "LegacyDeckAppearanceParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace CardStack;

namespace {

constexpr int RecordPayloadOffset = 5;
constexpr int SchemaFieldCountOffset = 87;
constexpr int FieldDefinitionSize = 37;
constexpr int FieldNameSize = 16;
constexpr int OldFormatOwnerFlagsOffset = 0x37;
constexpr quint8 OldFormatOwnerReadAccessFlag = 0x01;

void putU16(QByteArray* bytes, int offset, quint16 value)
{
    (*bytes)[offset] = static_cast<char>(value & 0xff);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putCString(QByteArray* bytes, int offset, int length, const QByteArray& value)
{
    for (int index = 0; index < length; ++index) {
        (*bytes)[offset + index] = index < value.size() ? value.at(index) : '\0';
    }
}

void putFieldDefinition(
    QByteArray* schema,
    int offset,
    const QByteArray& name,
    quint8 type,
    quint16 recordOffset,
    quint16 length)
{
    putCString(schema, offset, FieldNameSize, name);
    (*schema)[offset + 16] = static_cast<char>(type);
    putU16(schema, offset + 20, recordOffset);
    putU16(schema, offset + 22, length);
}

QByteArray makeSchemaRecord()
{
    QByteArray record(512, '\0');
    putCString(&record, 5, 5, "TN20");
    putCString(&record, 11, 64, "Synthetic Library");
    record[SchemaFieldCountOffset] = 3;

    const int fieldsOffset = 256;
    putFieldDefinition(&record, fieldsOffset, "Product", 1, 0, 30);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize, "Version", 1, 30, 10);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize * 2, "Notes", 3, 40, 40);
    return record;
}

QByteArray makeDeclaredCountSchemaRecord()
{
    QByteArray record(512, '\0');
    putCString(&record, 5, 5, "TN20");
    putCString(&record, 11, 64, "Declared Count Library");
    record[SchemaFieldCountOffset] = 2;

    const int fieldsOffset = 256;
    putFieldDefinition(&record, fieldsOffset, "Alpha", 1, 0, 12);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize, "Beta", 1, 12, 12);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize * 2, "ShouldSkip", 1, 24, 12);
    return record;
}

QByteArray makeCardRecord(const QByteArray& product, const QByteArray& version, const QByteArray& notes)
{
    const int notesBytes = std::max(40, int(notes.size()) + 1);
    QByteArray record(RecordPayloadOffset + 40 + notesBytes, '\0');
    putCString(&record, RecordPayloadOffset, 30, product);
    putCString(&record, RecordPayloadOffset + 30, 10, version);
    putCString(&record, RecordPayloadOffset + 40, notesBytes, notes);
    return record;
}

} // namespace

class LegacyDeckReaderTests : public QObject {
    Q_OBJECT

private slots:
    void decodesWin16TextAndPreservesCardWhitespace()
    {
        const QByteArray product = QByteArray("  Caf") + char(0xe9) + " " + char(0x99) + "  ";
        const QByteArray note = QByteArray("\r\n") + char(0x93) + "Quoted" + char(0x94) + "\r\n  ";
        const auto result = LegacyDeckReader{}.readRecords({makeSchemaRecord(), makeCardRecord(product, "1", note)});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("  Caf") + QChar(0x00e9) + QLatin1Char(' ') + QChar(0x2122) + QStringLiteral("  "));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("\r\n") + QChar(0x201c) + QStringLiteral("Quoted") + QChar(0x201d) + QStringLiteral("\r\n  "));
        const auto dos = LegacyDeckReader({}, LegacyTextEncoding::Dos437).readRecords({makeSchemaRecord(), makeCardRecord(QByteArray(1,char(0x82)), "1", "note")});
        QVERIFY(dos.ok());
        QCOMPARE(dos.deck.cardAt(0).valueAt(0), QString(QChar(0x00e9)));
    }

    void importsGroupedFieldsWithoutDroppingTheSchemaPrefix()
    {
        using namespace LegacyTemplateLayoutFormat;
        QByteArray schema(FieldTableOffset + 2 * FieldDescriptorSize, '\0');
        putCString(&schema, 5, 5, "TN20");
        putCString(&schema, 11, 64, "Grouped Fields");
        putU16(&schema, FieldCountOffset, 2);
        for (int field = 0; field < 2; ++field) {
            const int at = FieldTableOffset + field * FieldDescriptorSize;
            putCString(&schema, at, LegacyTemplateLayoutFormat::FieldNameSize, field == 0 ? "First" : "Second");
            putU16(&schema, at + FieldFlagsOffset, 0);
            putU16(&schema, at + FieldControlRoleOffset, field);
            putU16(&schema, at + FieldRecordOffsetOffset, field * 10);
            putU16(&schema, at + FieldDataLengthOffset, 10);
            putU16(&schema, at + FieldRightOffset, 200);
            putU16(&schema, at + FieldBottomOffset, 20);
        }
        QByteArray card(RecordPayloadOffset + 20, '\0');
        putCString(&card, RecordPayloadOffset, 10, "Alpha");
        putCString(&card, RecordPayloadOffset + 10, 10, "Beta");
        const auto result = LegacyDeckReader{}.readRecords({schema, card});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.fieldCount(), 2);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("First"));
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("Beta"));
        putU16(&schema, FieldCountOffset, 3);
        QVERIFY(!LegacyDeckReader{}.readRecords({schema, card}).ok());
    }

    void importsVariableNotesBeforeLaterFixedFieldsInDesignerOrder()
    {
        using namespace LegacyTemplateLayoutFormat;
        QByteArray schema(FieldTableOffset + 3 * FieldDescriptorSize, '\0');
        putCString(&schema, 5, 5, "TN20");
        putCString(&schema, 11, 64, "Unordered Fields");
        putU16(&schema, FieldCountOffset, 3);
        for (int field = 0; field < 3; ++field) {
            const int at = FieldTableOffset + field * FieldDescriptorSize;
            putCString(&schema, at, LegacyTemplateLayoutFormat::FieldNameSize, field == 0 ? "First" : field == 1 ? "Notes" : "Second");
            putU16(&schema, at + FieldFlagsOffset, field == 1 ? VariableTextFlag : 0);
            putU16(&schema, at + FieldControlRoleOffset, field == 0 ? 0 : 1);
            putU16(&schema, at + FieldRecordOffsetOffset, field == 0 ? 0 : field == 1 ? 20 : 10);
            putU16(&schema, at + FieldDataLengthOffset, field == 1 ? 40 : 10);
            putU16(&schema, at + FieldRightOffset, 200);
            putU16(&schema, at + FieldBottomOffset, 20);
        }
        const QByteArray note(500, 'N');
        QByteArray card(RecordPayloadOffset + 20 + note.size() + 1, '\0');
        putCString(&card, RecordPayloadOffset, 10, "Alpha");
        putCString(&card, RecordPayloadOffset + 10, 10, "Beta");
        putCString(&card, RecordPayloadOffset + 20, note.size() + 1, note);
        const auto result = LegacyDeckReader{}.readRecords({schema, card});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.fieldCount(), 3);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QString::fromLatin1(note));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Beta"));
        QVERIFY(result.deck.fieldAt(1).isNotes());
    }

    void importsFullVariableNotesRatherThanTheStorageReservation()
    {
        const QByteArray notes = QByteArray("  ") + QByteArray(6000, 'N') + "\r\n  ";
        const auto result = LegacyDeckReader{}.readRecords({makeSchemaRecord(), makeCardRecord("Title", "1", notes)});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QString::fromLatin1(notes));
        QCOMPARE(result.deck.fields().at(2).maxLength(), DefaultNotesMaximumLength);
        const auto maximum = LegacyDeckReader{}.readRecords({makeSchemaRecord(), makeCardRecord("Title", "1", QByteArray(DefaultNotesMaximumLength, 'N'))});
        QVERIFY(maximum.ok());
        QCOMPARE(maximum.deck.cardAt(0).valueAt(2).size(), DefaultNotesMaximumLength);
    }

    void mapsWinIndexSavedLabelsBoundsFontsAndSortOrder()
    {
        QByteArray schema(0x842, '\0');
        schema.replace(5, 6, "WINDEX");
        schema.replace(11, 13, "Custom layout");
        putU16(&schema, 0x49, 640); putU16(&schema, 0x4b, 480);
        putU16(&schema, 0x4d, 2); putU16(&schema, 0x4f, 0x0040);
        schema[0x59] = 2; putU16(&schema, 0x5c, 1);
        putCString(&schema, 0x5e, 30, "Saved label");
        putU16(&schema, 0x7c, 3); putU16(&schema, 0x7e, 3);
        putU16(&schema, 0x80, 30); putU16(&schema, 0x82, 12);
        putU16(&schema, 0x2ce, quint16(qint16(-10)));
        putU16(&schema, 0x29c, quint16(qint16(-12)));
        for (int i = 0; i < 2; ++i) {
            const int at = 0x3cc + i * 0x39;
            putCString(&schema, at, 30, i == 0 ? "Subject" : "Journal");
            putU16(&schema, at + 0x1e, 1); putU16(&schema, at + 0x20, i == 0 ? 1 : 0);
            putU16(&schema, at + 0x22, i * 20); putU16(&schema, at + 0x24, i == 0 ? 20 : 40);
            putU16(&schema, at + 0x27, i == 0 ? 0 : 3);
            putU16(&schema, at + 0x31, 33); putU16(&schema, at + 0x33, i == 0 ? 30 : 60);
            putU16(&schema, at + 0x35, i == 0 ? 90 : 180); putU16(&schema, at + 0x37, i == 0 ? 18 : 60);
        }
        const auto result = LegacyDeckReader{}.readRecords({schema});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        const auto& layout = result.deck.cardTemplateLayout();
        QCOMPARE(layout.canvasWidth, 6390); QCOMPARE(layout.canvasHeight, 4800);
        QCOMPARE(layout.frames.size(), 3);
        QCOMPARE(layout.frames[0].kind, CardTemplateFrameKind::Text);
        QCOMPARE(layout.frames[0].text, QStringLiteral("Saved label"));
        QCOMPARE(layout.frames[0].bounds, QRect(30, 210, 300, 120));
        QCOMPARE(layout.frames[1].bounds, QRect(330, 480, 900, 180));
        QCOMPARE(layout.frames[2].bounds, QRect(330, 780, 1800, 600));
        QCOMPARE(layout.frames[2].kind, CardTemplateFrameKind::NotesBox);
        QCOMPARE(result.deck.fieldAt(1).maxLength(), DefaultNotesMaximumLength);
        QVERIFY(result.deck.fieldAt(0).isPhone());
        QCOMPARE(result.deck.sortKeys().size(), 1);
        QCOMPARE(result.deck.sortKeys().first().fieldIndex, 1);
        QVERIFY(result.deck.sortKeys().first().descending);
        QFont data, name;
        QVERIFY(data.fromString(result.deck.appearance().dataFont));
        QVERIFY(name.fromString(result.deck.appearance().nameFont));
        QCOMPARE(data.family(), QStringLiteral("Arial")); QCOMPARE(data.pixelSize(), 10);
        QCOMPARE(name.family(), QStringLiteral("Arial")); QCOMPARE(name.pixelSize(), 12);
        QCOMPARE(result.deck.appearance().textFont, result.deck.appearance().dataFont);
        QCOMPARE(result.deck.appearance().indexFont, result.deck.appearance().nameFont);
        QVERIFY(result.deck.appearance().useSystemColors);
        putU16(&schema, 0x3cc + 0x31, 4); putU16(&schema, 0x3cc + 0x33, 1);
        putU16(&schema, 0x3cc + 0x35, 20); putU16(&schema, 0x3cc + 0x37, 11);
        putU16(&schema, 0x405 + 0x33, 22); putU16(&schema, 0x405 + 0x37, 10);
        const auto snapped = LegacyDeckReader{}.readRecords({schema});
        QVERIFY2(snapped.ok(), qPrintable(snapped.errorMessage));
        QCOMPARE(snapped.deck.cardTemplateLayout().frames[1].bounds, QRect(30, 180, 210, 120));
        QCOMPARE(snapped.deck.cardTemplateLayout().frames[2].bounds.top(), 390);
        QCOMPARE(snapped.deck.cardTemplateLayout().frames[2].bounds.height(), 90);
        QCOMPARE(snapped.deck.cardTemplateLayout().canvasHeight, 4770);
    }

    void mapsWinIndexFieldsAndVariableNotesWithoutHardCodedNames()
    {
        constexpr int TableOffset = 0x3cc, DescriptorSize = 0x39;
        QByteArray schema(0x842, '\0');
        schema.replace(5, 6, "WINDEX");
        schema.replace(11, 12, "Custom notes");
        schema[89] = 3;
        const QList<QByteArray> names = {"Subject", "Owner", "Journal"};
        for (int i = 0; i < names.size(); ++i) {
            const int offset = TableOffset + i * DescriptorSize;
            schema.replace(offset, names.at(i).size(), names.at(i));
            putU16(&schema, offset + 0x1e, 1);
            putU16(&schema, offset + 0x22, i * 20);
            putU16(&schema, offset + 0x24, i == 2 ? 40 : 20);
            putU16(&schema, offset + 0x27, i == 2 ? 3 : 0);
        }
        const QByteArray notes(4096, 'N');
        QByteArray card(5 + 40, '\0');
        card.replace(5, 7, "Subject"); card.replace(25, 5, "Owner");
        card += notes; card += '\0';
        const auto result = LegacyDeckReader{}.readRecords({schema, card});
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.fieldCount(), 3);
        QCOMPARE(result.deck.fields().at(2).name(), QStringLiteral("Journal"));
        QCOMPARE(result.deck.fields().at(2).maxLength(), DefaultNotesMaximumLength);
        QCOMPARE(result.deck.cardCount(), 1);
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QString::fromLatin1(notes));
        schema[89] = 21;
        QVERIFY(!LegacyDeckReader{}.readRecords({schema, card}).ok());
    }

    void validatesFixtureCorpusManifestWhenConfigured()
    {
        const QString root = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        if (root.isEmpty()) QSKIP("Set the fixture directory to validate the corpus contract.");
        qInfo("Corpus directory: %s", qPrintable(QFileInfo(root).absoluteFilePath()));
        QDir directory(root);
        QFile manifest(directory.filePath(QStringLiteral("corpus-manifest.json")));
        QVERIFY2(manifest.open(QIODevice::ReadOnly), qPrintable(manifest.fileName()));
        const QJsonObject contract = QJsonDocument::fromJson(manifest.readAll()).object();
        QCOMPARE(contract.value(QStringLiteral("version")).toInt(), 1);
        const auto files = contract.value(QStringLiteral("files")).toArray();
        QVERIFY(!files.isEmpty());
        for (const QJsonValue& value : files) {
            const QJsonObject spec = value.toObject();
            const QString name = spec.value(QStringLiteral("name")).toString();
            QVERIFY(!name.isEmpty() && QFileInfo(name).fileName() == name && name != QLatin1String(".."));
            QFile file(directory.filePath(name));
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            const QByteArray bytes = file.readAll();
            QCOMPARE(bytes.size(), qsizetype(spec.value(QStringLiteral("size")).toInteger()));
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()), spec.value(QStringLiteral("sha256")).toString());
            QVERIFY(!spec.value(QStringLiteral("origin")).toString().isEmpty());
        }
        QFile evidence(directory.filePath(QStringLiteral("generation-evidence.json")));
        QVERIFY2(evidence.open(QIODevice::ReadOnly), qPrintable(evidence.fileName()));
        const auto generated = QJsonDocument::fromJson(evidence.readAll()).object();
        QCOMPARE(generated.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(generated.value(QStringLiteral("inputMode")).toString(), QStringLiteral("window-messages-only"));
        const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
        QVERIFY(sha256.match(generated.value(QStringLiteral("sourceExeSha256")).toString()).hasMatch());
        QVERIFY(sha256.match(generated.value(QStringLiteral("scenarioSha256")).toString()).hasMatch());
        QSet<QString> authoredRecipes;
        const auto runs = generated.value(QStringLiteral("runs")).toArray();
        QVERIFY(!runs.isEmpty());
        for (const auto& value : runs) {
            const auto run = value.toObject();
            const QString recipe = run.value(QStringLiteral("fixture")).toString();
            QVERIFY(!recipe.isEmpty()); QVERIFY(!authoredRecipes.contains(recipe));
            QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("passed"));
            QCOMPARE(run.value(QStringLiteral("exitCode")).toInt(-1), 0);
            QCOMPARE(run.value(QStringLiteral("inputMode")).toString(), QStringLiteral("window-messages-only"));
            QVERIFY(run.value(QStringLiteral("stepCount")).toInt() > 0);
            authoredRecipes.insert(recipe);
        }
        const auto gates = contract.value(QStringLiteral("coverage")).toArray();
        const bool complete = !gates.isEmpty() && std::all_of(gates.cbegin(), gates.cend(), [](const QJsonValue& gate) {
            return gate.toObject().value(QStringLiteral("status")).toString() == QLatin1String("verified");
        });
        if (complete) for (const auto& value : files) {
            const auto file = value.toObject();
            if (file.value(QStringLiteral("origin")).toString() != QLatin1String("original-ui")) continue;
            QVERIFY(authoredRecipes.contains(file.value(QStringLiteral("recipe")).toString()));
            QCOMPARE(file.value(QStringLiteral("sourceExeSha256")), generated.value(QStringLiteral("sourceExeSha256")));
            QCOMPARE(file.value(QStringLiteral("scenarioSha256")), generated.value(QStringLiteral("scenarioSha256")));
        }
        const auto cases = contract.value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject spec = value.toObject();
            const QString file = directory.filePath(spec.value(QStringLiteral("file")).toString());
            qInfo("Corpus case: %s", qPrintable(spec.value(QStringLiteral("id")).toString()));
            if (spec.value(QStringLiteral("format")).toString() == QLatin1String("reports")) {
                const auto result = LegacyReportReader{}.readFile(file);
                QVERIFY2(result.ok(), qPrintable(result.errorMessage));
                QCOMPARE(result.reports.size(), spec.value(QStringLiteral("reportCount")).toInt());
                continue;
            }
            const auto encoding = spec.value(QStringLiteral("encoding")).toString() == QLatin1String("dos437")
                ? LegacyTextEncoding::Dos437 : LegacyTextEncoding::Windows1252;
            LegacyDeckReader::Result result;
            if (spec.value(QStringLiteral("format")).toString() == QLatin1String("interchange")) {
                const auto imported = LegacyInterchangeReader{}.readFile(file, spec.value(QStringLiteral("password")).toString());
                result.deck = imported.deck;
                result.errorMessage = imported.errorMessage;
                result.legacyPasswordProtected = imported.legacyPasswordProtected;
                result.legacyPasswordVerified = imported.legacyPasswordVerified;
            } else {
                QCOMPARE(spec.value(QStringLiteral("format")).toString(), QStringLiteral("deck"));
                result = LegacyDeckReader({}, encoding).readDeck(file, spec.value(QStringLiteral("password")).toString());
            }
            if (spec.value(QStringLiteral("reject")).toBool()) { QVERIFY(!result.ok()); QVERIFY(!result.errorMessage.isEmpty()); continue; }
            QVERIFY2(result.ok(), qPrintable(result.errorMessage));
            if (spec.contains(QStringLiteral("name"))) QCOMPARE(result.deck.name(), spec.value(QStringLiteral("name")).toString());
            if (spec.contains(QStringLiteral("fieldCount"))) QCOMPARE(result.deck.fieldCount(), spec.value(QStringLiteral("fieldCount")).toInt());
            if (spec.contains(QStringLiteral("cardCount"))) QCOMPARE(result.deck.cardCount(), spec.value(QStringLiteral("cardCount")).toInt());
            const auto expectedNames = spec.value(QStringLiteral("fieldNames")).toArray();
            if (!expectedNames.isEmpty()) {
                QCOMPARE(result.deck.fieldCount(), expectedNames.size());
                for (int field = 0; field < expectedNames.size(); ++field)
                    QCOMPARE(result.deck.fields().at(field).name(), expectedNames.at(field).toString());
            }
            if (spec.contains(QStringLiteral("fieldMaxLength")))
                for (const auto& field : result.deck.fields()) QCOMPARE(field.maxLength(), spec.value(QStringLiteral("fieldMaxLength")).toInt());
            if (spec.contains(QStringLiteral("layoutFieldCount"))) {
                QSet<int> linkedFields;
                for (const auto& frame : result.deck.cardTemplateLayout().frames) {
                    if (frame.fieldIndex < 0) continue;
                    QVERIFY(frame.fieldIndex < result.deck.fieldCount());
                    QVERIFY(frame.bounds.isValid());
                    QVERIFY(!linkedFields.contains(frame.fieldIndex));
                    linkedFields.insert(frame.fieldIndex);
                }
                QCOMPARE(linkedFields.size(), spec.value(QStringLiteral("layoutFieldCount")).toInt());
            }
            if (spec.contains(QStringLiteral("protected"))) QCOMPARE(result.legacyPasswordProtected, spec.value(QStringLiteral("protected")).toBool());
            if (spec.contains(QStringLiteral("encrypted"))) QCOMPARE(result.legacyDataEncrypted, spec.value(QStringLiteral("encrypted")).toBool());
            if (spec.contains(QStringLiteral("verified"))) QCOMPARE(result.legacyPasswordVerified, spec.value(QStringLiteral("verified")).toBool());
            if (spec.contains(QStringLiteral("stockTemplate"))) {
                const auto stock = findBuiltInDeckTemplate(spec.value(QStringLiteral("stockTemplate")).toString());
                QVERIFY(stock.has_value());
                (void)QTest::qCompare(result.deck.fieldCount(), stock->fields.size(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                for (int field = 0; field < std::min(result.deck.fieldCount(), int(stock->fields.size())); ++field) {
                    const auto& actual = result.deck.fields().at(field);
                    const auto& expected = stock->fields.at(field);
                    (void)QTest::qCompare(actual.name(), expected.name(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.type(), expected.type(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.maxLength(), expected.maxLength(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.isPhone(), expected.isPhone(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.showName(), expected.showName(), "Imported label mode", "Built-in label mode", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.displayWidth(), expected.displayWidth(), "Imported display width", "Built-in display width", __FILE__, __LINE__);
                }
                (void)QTest::qCompare(result.deck.sortKeys(), stock->sortKeys, "Imported sort keys", "Built-in sort keys", __FILE__, __LINE__);
                const auto& actualLayout = result.deck.cardTemplateLayout();
                (void)QTest::qCompare(actualLayout.canvasWidth, stock->layout.canvasWidth, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                (void)QTest::qCompare(actualLayout.canvasHeight, stock->layout.canvasHeight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                (void)QTest::qCompare(actualLayout.frames.size(), stock->layout.frames.size(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                for (int frame = 0; frame < std::min(actualLayout.frames.size(), stock->layout.frames.size()); ++frame) {
                    const auto& actual = actualLayout.frames.at(frame);
                    const auto& expected = stock->layout.frames.at(frame);
                    (void)QTest::qCompare(actual.kind, expected.kind, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.bounds, expected.bounds, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.text, expected.text, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.fieldIndex, expected.fieldIndex, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    (void)QTest::qCompare(actual.styleFlags, expected.styleFlags, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                }
                for (int row = 0; row < result.deck.cardCount(); ++row)
                    for (int field = 0; field < result.deck.fieldCount(); ++field)
                        QVERIFY(result.deck.cardAt(row).valueAt(field).isEmpty());
                if (spec.contains(QStringLiteral("reportFile"))) {
                    QStringList fieldNames;
                    for (const auto& field : result.deck.fields()) fieldNames.append(field.name());
                    const auto sidecar = LegacyReportReader{}.readFile(directory.filePath(spec.value(QStringLiteral("reportFile")).toString()), fieldNames);
                    QVERIFY2(sidecar.ok(), qPrintable(sidecar.errorMessage));
                    (void)QTest::qCompare(sidecar.reports.size(), stock->reports.size(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                    for (int report = 0; report < std::min(sidecar.reports.size(), stock->reports.size()); ++report) {
                        const auto& actual = sidecar.reports.at(report);
                        const auto& expected = stock->reports.at(report);
                        (void)QTest::qCompare(actual.name, expected.name, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.formType, expected.formType, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.formWidth, expected.formWidth, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.formHeight, expected.formHeight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.headerHeight, expected.headerHeight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.footerHeight, expected.footerHeight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.rows, expected.rows, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.columns, expected.columns, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.marginLeft, expected.marginLeft, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.marginTop, expected.marginTop, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.marginRight, expected.marginRight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.marginBottom, expected.marginBottom, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.horizontalGutter, expected.horizontalGutter, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.verticalGutter, expected.verticalGutter, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.paperStyleId, expected.paperStyleId, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.pageWidth, expected.pageWidth, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.pageHeight, expected.pageHeight, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.orientation, expected.orientation, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.dataFont, expected.dataFont, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.textFont, expected.textFont, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        (void)QTest::qCompare(actual.frames.size(), expected.frames.size(), "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        for (int frame = 0; frame < std::min(actual.frames.size(), expected.frames.size()); ++frame) {
                            const auto& actualFrame = actual.frames.at(frame);
                            const auto& expectedFrame = expected.frames.at(frame);
                            (void)QTest::qCompare(actualFrame.kind, expectedFrame.kind, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.fieldPlaceholders, expectedFrame.fieldPlaceholders, "Imported field bindings", "Built-in field bindings", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.bounds, expectedFrame.bounds, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.text, expectedFrame.text, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.styleFlags, expectedFrame.styleFlags, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.lineBoxShape, expectedFrame.lineBoxShape, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.lineStyle, expectedFrame.lineStyle, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.fillPattern, expectedFrame.fillPattern, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.cornerRadius, expectedFrame.cornerRadius, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.band, expectedFrame.band, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.printEntireContentsFlag, expectedFrame.printEntireContentsFlag, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                            (void)QTest::qCompare(actualFrame.validationFlags, expectedFrame.validationFlags, "Imported stock value", "Built-in stock value", __FILE__, __LINE__);
                        }
                    }
                }
            }
            for (const QJsonValue& required : spec.value(QStringLiteral("requiredText")).toArray()) {
                bool found = false;
                for (int row = 0; row < result.deck.cardCount(); ++row)
                    for (int field = 0; field < result.deck.fieldCount(); ++field)
                        found = found || result.deck.cardAt(row).valueAt(field).contains(required.toString());
                QVERIFY2(found, "A required authored value was not retained in the imported data.");
            }
            const auto expectedRows = spec.value(QStringLiteral("rows")).toArray();
            if (!expectedRows.isEmpty()) {
                QCOMPARE(result.deck.cardCount(), expectedRows.size());
                if (spec.value(QStringLiteral("rowsUnordered")).toBool()) {
                    QList<QByteArray> actualRows, contractRows;
                    for (int row = 0; row < result.deck.cardCount(); ++row) {
                        QJsonArray cells;
                        for (int field = 0; field < result.deck.fieldCount(); ++field) cells.append(result.deck.cardAt(row).valueAt(field));
                        actualRows.append(QJsonDocument(cells).toJson(QJsonDocument::Compact));
                        contractRows.append(QJsonDocument(expectedRows.at(row).toArray()).toJson(QJsonDocument::Compact));
                    }
                    std::sort(actualRows.begin(), actualRows.end());
                    std::sort(contractRows.begin(), contractRows.end());
                    QCOMPARE(actualRows, contractRows);
                    continue;
                }
                for (int row = 0; row < expectedRows.size(); ++row) {
                    const auto cells = expectedRows.at(row).toArray();
                    QCOMPARE(result.deck.fieldCount(), cells.size());
                    for (int field = 0; field < cells.size(); ++field) QCOMPARE(result.deck.cardAt(row).valueAt(field), cells.at(field).toString());
                }
            }
        }
    }

    void recoveredTextStyleBitsUseTheirExactMeanings()
    {
        QCOMPARE(CardTemplateStyleFlagUnderline, quint8(0x01));
        QCOMPARE(CardTemplateStyleFlagBold, quint8(0x02));
        QCOMPARE(CardTemplateStyleFlagItalic, quint8(0x04));
        QCOMPARE(ReportStyleFlagUnderline, quint8(0x01));
        QCOMPARE(ReportStyleFlagBold, quint8(0x02));
        QCOMPARE(ReportStyleFlagItalic, quint8(0x04));
    }

    void decodesRecoveredDeckAppearanceOffsets()
    {
        using namespace LegacyDeckAppearanceFormat;
        QByteArray record(MinimumAppearanceRecordSize, '\0');
        const auto putU16 = [&record](int offset, quint16 value) {
            record[offset] = static_cast<char>(value & 0xff);
            record[offset + 1] = static_cast<char>((value >> 8) & 0xff);
        };
        const auto putColorRef = [&putU16](int offset, quint32 value) {
            putU16(offset, static_cast<quint16>(value & 0xffff));
            putU16(offset + 2, static_cast<quint16>((value >> 16) & 0xffff));
        };
        const auto putFont = [&record, &putU16](int offset, const QByteArray& family, int height, int weight) {
            putU16(offset + FontHeightOffset, static_cast<quint16>(static_cast<qint16>(height)));
            putU16(offset + FontWeightOffset, static_cast<quint16>(weight));
            record[offset + FontItalicOffset] = 1;
            const QByteArray face = family.left(FontFaceNameLength - 1);
            std::copy(face.cbegin(), face.cend(), record.begin() + offset + FontFaceNameOffset);
        };

        putFont(IndexFontOffset, "Index Face", -13, 400);
        putFont(NameFontOffset, "Name Face", -14, 500);
        putFont(DataFontOffset, "Data Face", -15, 600);
        putFont(TextFontOffset, "Text Face", -16, 700);
        putColorRef(IndexForegroundColorOffset, 0x00332211);
        putColorRef(DataForegroundColorOffset, 0x00665544);
        putColorRef(NameForegroundColorOffset, 0x00998877);
        putColorRef(TextForegroundColorOffset, 0x00ccbbaa);
        putColorRef(DataBackgroundColorOffset, 0x00030201);
        putColorRef(IndexBackgroundColorOffset, 0x00060504);
        putColorRef(CardBackgroundColorOffset, 0x00090807);
        putU16(UseSystemColorsOffset, 0);

        const DeckAppearance appearance = parse(record);
        QFont font;
        QVERIFY(font.fromString(appearance.indexFont));
        QCOMPARE(font.family(), QStringLiteral("Index Face"));
        QCOMPARE(font.pixelSize(), 13);
        QVERIFY(font.italic());
        QVERIFY(font.fromString(appearance.nameFont));
        QCOMPARE(font.family(), QStringLiteral("Name Face"));
        QVERIFY(font.fromString(appearance.dataFont));
        QCOMPARE(font.family(), QStringLiteral("Data Face"));
        QVERIFY(font.fromString(appearance.textFont));
        QCOMPARE(font.family(), QStringLiteral("Text Face"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::IndexForeground)), QStringLiteral("#112233"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::DataForeground)), QStringLiteral("#445566"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::NameForeground)), QStringLiteral("#778899"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::TextForeground)), QStringLiteral("#aabbcc"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::DataBackground)), QStringLiteral("#010203"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::IndexBackground)), QStringLiteral("#040506"));
        QCOMPARE(appearance.customColors.at(static_cast<int>(DeckColorRole::CardBackground)), QStringLiteral("#070809"));
        QVERIFY(!appearance.useSystemColors);
    }

    void mapsCurrentVisualTemplateDescriptorRecord()
    {
        using namespace LegacyTemplateLayoutFormat;
        QByteArray record(FieldTableOffset + FieldDescriptorSize * 2, '\0');
        putU16(&record, TextFrameCountOffset, 1);
        putU16(&record, FieldCountOffset, 2);

        putCString(&record, TextFrameTableOffset, TextFrameTextSize, "Index");
        putU16(&record, TextFrameTableOffset + TextFrameRightOffset, 640);
        putU16(&record, TextFrameTableOffset + TextFrameBottomOffset, 22);

        const int nameOffset = FieldTableOffset;
        putCString(&record, nameOffset, LegacyTemplateLayoutFormat::FieldNameSize, "Name");
        putU16(&record, nameOffset + FieldControlRoleOffset, StandaloneControlRole);
        putU16(&record, nameOffset + FieldDialableMarkerOffset, 1);
        putU16(&record, nameOffset + FieldDataLengthOffset, 30);
        putU16(&record, nameOffset + FieldLeftOffset, 60);
        putU16(&record, nameOffset + FieldTopOffset, 30);
        putU16(&record, nameOffset + FieldRightOffset, 300);
        putU16(&record, nameOffset + FieldBottomOffset, 55);
        putU16(&record, nameOffset + FieldDisplayWidthOffset, 279);
        record[nameOffset + FieldAlignmentOffset] = AlignRight;

        const int notesOffset = FieldTableOffset + FieldDescriptorSize;
        putCString(&record, notesOffset, LegacyTemplateLayoutFormat::FieldNameSize, "Notes");
        putU16(&record, notesOffset + FieldFlagsOffset, NotesFlags);
        putU16(&record, notesOffset + FieldControlRoleOffset, StandaloneControlRole);
        putU16(&record, notesOffset + FieldDataLengthOffset, 40);
        putU16(&record, notesOffset + FieldLeftOffset, 60);
        putU16(&record, notesOffset + FieldTopOffset, 60);
        putU16(&record, notesOffset + FieldRightOffset, 300);
        putU16(&record, notesOffset + FieldBottomOffset, 120);

        const auto parsed = parse(record, 2);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->fields.size(), 2);
        QCOMPARE(parsed->fields.at(0).name(), QStringLiteral("Name"));
        QVERIFY(parsed->fields.at(0).showName());
        QVERIFY(parsed->fields.at(0).isPhone());
        QCOMPARE(parsed->fields.at(0).displayWidth(), 279);
        QVERIFY(parsed->fields.at(1).isNotes());
        QCOMPARE(parsed->layout.frames.size(), 3);
        QCOMPARE(parsed->layout.frames.at(0).text, QStringLiteral("Index"));
        QCOMPARE(parsed->layout.frames.at(1).bounds, QRect(600, 300, 2400, 250));
        QVERIFY((parsed->layout.frames.at(1).styleFlags & CardTemplateStyleFlagAlignRight) != 0);
        QCOMPARE(parsed->layout.frames.at(1).legacyDescriptor.size(), FieldDescriptorSize);
    }

    void mapsLegacyFieldDefinitionsAndFixedRecords()
    {
        const QVector<QByteArray> records = {
            makeSchemaRecord(),
            makeCardRecord("ProjectKit", "1.0", "Legacy card"),
            makeCardRecord("Windows", "3.x", "Second card"),
            QByteArray("\0\0\0\0\0~BT~LO2", 12),
        };

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readRecords(records, QStringLiteral("Fallback"));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.deck.name(), QStringLiteral("Synthetic Library"));
        QCOMPARE(result.deck.fieldCount(), 3);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Product"));
        QCOMPARE(result.deck.fieldAt(0).type(), FieldType::Text);
        QCOMPARE(result.deck.fieldAt(0).maxLength(), 30);
        QCOMPARE(result.deck.fieldAt(2).name(), QStringLiteral("Notes"));
        QCOMPARE(result.deck.fieldAt(2).type(), FieldType::Notes);
        QCOMPARE(result.deck.cardCount(), 2);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("ProjectKit"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Legacy card"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("Windows"));
        QCOMPARE(result.deck.cardAt(1).valueAt(2), QStringLiteral("Second card"));
    }

    void honorsDeclaredFieldCountWithoutProductAnchor()
    {
        QByteArray card(RecordPayloadOffset + 36, '\0');
        putCString(&card, RecordPayloadOffset, 12, "first");
        putCString(&card, RecordPayloadOffset + 12, 12, "second");
        putCString(&card, RecordPayloadOffset + 24, 12, "ignored");

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readRecords(
            {makeDeclaredCountSchemaRecord(), card},
            QStringLiteral("Fallback"));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.deck.name(), QStringLiteral("Declared Count Library"));
        QCOMPARE(result.deck.fieldCount(), 2);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Alpha"));
        QCOMPARE(result.deck.fieldAt(1).name(), QStringLiteral("Beta"));
        QCOMPARE(result.deck.cardCount(), 1);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("first"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("second"));
    }

    void importsSoftwareSampleWhenConfigured()
    {
        const QString samplePath = qEnvironmentVariable("CARDSTACK_LEGACY_DECK_SAMPLE");
        if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) {
            QSKIP("Set CARDSTACK_LEGACY_DECK_SAMPLE to a legacy .BTN file to run the real legacy import check.");
        }
        const QString goldenDirectory = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        if (!goldenDirectory.isEmpty() &&
            QFileInfo(samplePath).canonicalFilePath() == QFileInfo(QDir(goldenDirectory).filePath(QStringLiteral("plain.BTN"))).canonicalFilePath()) {
            QSKIP("The canonical plain.BTN sample is covered by importsWineVdmGoldenFixturesWhenConfigured.");
        }

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readDeck(samplePath);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.btrieveMetadata.version, 0x0500);
        QCOMPARE(result.btrieveMetadata.pageSize, 4096);
        QCOMPARE(result.btrieveMetadata.fixedRecordLength, 329);
        QCOMPARE(result.rawRecords.size(), 5);
        QCOMPARE(result.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(result.deck.fieldCount(), 11);
        QVERIFY(result.deck.cardTemplateLayout().frames.size() >= result.deck.fieldCount());
        QStringList importedProducts;
        for (int index = 0; index < result.deck.cardCount(); ++index) {
            importedProducts.append(result.deck.cardAt(index).valueAt(0));
        }
        QVERIFY2(
            result.deck.cardCount() == 3,
            qPrintable(QStringLiteral("Imported Product values: %1").arg(importedProducts.join(QStringLiteral(" | ")))));
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Product"));
        QCOMPARE(result.deck.fieldAt(10).name(), QStringLiteral("Notes"));
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Button") + QStringLiteral("Ware Incorporated"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("Windows") + QChar(0x2122));
        QCOMPARE(result.deck.cardAt(2).valueAt(0), QStringLiteral("other software"));
    }

    void importsWineVdmGoldenFixturesWhenConfigured()
    {
        const QString fixtureDirPath = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        if (fixtureDirPath.isEmpty() || !QDir(fixtureDirPath).exists()) {
            QSKIP("Set CARDSTACK_WINEVDM_GOLDEN_DIR to tests/fixtures/legacy/winevdm or another WineVDM fixture directory.");
        }

        const QDir fixtureDir(fixtureDirPath);
        const QString plainPath = fixtureDir.filePath(QStringLiteral("plain.BTN"));
        const QString passwordOnlyPath = fixtureDir.filePath(QStringLiteral("pwonly.BTN"));
        const QString encryptedPath = fixtureDir.filePath(QStringLiteral("crypt.BTN"));
        const QString notesHeavyPath = fixtureDir.filePath(QStringLiteral("notes_heavy.BTN"));
        const QString manyFieldsPath = fixtureDir.filePath(QStringLiteral("many_fields.BTN"));
        const QString maxLengthsPath = fixtureDir.filePath(QStringLiteral("max_lengths.BTN"));
        const QString securityCyclePath = fixtureDir.filePath(QStringLiteral("security_cycle.BTN"));
        const QString oemTextPath = fixtureDir.filePath(QStringLiteral("oem_text.BTN"));
        const QString unusualIndexPath = fixtureDir.filePath(QStringLiteral("unusual_index.BTN"));
        const QString damagedTruncatedPath = fixtureDir.filePath(QStringLiteral("damaged_truncated.BTN"));
        QVERIFY2(QFileInfo::exists(plainPath), qPrintable(QStringLiteral("Missing %1").arg(plainPath)));
        QVERIFY2(QFileInfo::exists(passwordOnlyPath), qPrintable(QStringLiteral("Missing %1").arg(passwordOnlyPath)));
        QVERIFY2(QFileInfo::exists(encryptedPath), qPrintable(QStringLiteral("Missing %1").arg(encryptedPath)));
        QVERIFY2(QFileInfo::exists(notesHeavyPath), qPrintable(QStringLiteral("Missing %1").arg(notesHeavyPath)));
        QVERIFY2(QFileInfo::exists(manyFieldsPath), qPrintable(QStringLiteral("Missing %1").arg(manyFieldsPath)));
        QVERIFY2(QFileInfo::exists(maxLengthsPath), qPrintable(QStringLiteral("Missing %1").arg(maxLengthsPath)));
        QVERIFY2(QFileInfo::exists(securityCyclePath), qPrintable(QStringLiteral("Missing %1").arg(securityCyclePath)));
        QVERIFY2(QFileInfo::exists(oemTextPath), qPrintable(QStringLiteral("Missing %1").arg(oemTextPath)));
        QVERIFY2(QFileInfo::exists(unusualIndexPath), qPrintable(QStringLiteral("Missing %1").arg(unusualIndexPath)));
        QVERIFY2(QFileInfo::exists(damagedTruncatedPath), qPrintable(QStringLiteral("Missing %1").arg(damagedTruncatedPath)));

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result plain = reader.readDeck(plainPath);
        QVERIFY2(plain.ok(), qPrintable(plain.errorMessage));
        QCOMPARE(plain.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(plain.deck.fieldCount(), 11);
        QCOMPARE(plain.deck.cardCount(), 3);
        QCOMPARE(plain.deck.fieldAt(10).name(), QStringLiteral("Notes"));
        QCOMPARE(plain.deck.cardAt(1).valueAt(0), QStringLiteral("Windows") + QChar(0x2122));
        QVERIFY(!plain.deck.legacyControlRecord().isEmpty());
        const DeckAppearance& plainAppearance = plain.deck.appearance();
        QVERIFY(!plainAppearance.indexFont.isEmpty());
        QVERIFY(!plainAppearance.nameFont.isEmpty());
        QVERIFY(!plainAppearance.dataFont.isEmpty());
        QVERIFY(!plainAppearance.textFont.isEmpty());
        QCOMPARE(plainAppearance.customColors.size(), static_cast<int>(DeckColorRole::Count));
        for (const QString& colorName : plainAppearance.customColors) {
            QVERIFY2(QColor::isValidColorName(colorName), qPrintable(colorName));
        }
        for (const FieldDefinition& field : plain.deck.fields()) {
            QCOMPARE(field.legacyDescriptor().size(), LegacyTemplateLayoutFormat::FieldDescriptorSize);
        }

        const LegacyDeckReader::Result passwordMissing = reader.readDeck(passwordOnlyPath);
        QVERIFY(passwordMissing.passwordRequired);
        QVERIFY(!passwordMissing.passwordRejected);

        const LegacyDeckReader::Result passwordOnly = reader.readDeck(passwordOnlyPath, QStringLiteral("Legacy01"));
        QVERIFY2(passwordOnly.ok(), qPrintable(passwordOnly.errorMessage));
        QVERIFY(passwordOnly.legacyPasswordProtected);
        QVERIFY(passwordOnly.legacyPasswordVerified);
        QVERIFY(!passwordOnly.legacyPasswordVerificationUnavailable);
        QVERIFY(passwordOnly.deck.description().contains(QStringLiteral("password-protected")));
        QCOMPARE(passwordOnly.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(passwordOnly.deck.fieldCount(), 11);
        QCOMPARE(passwordOnly.deck.cardCount(), 3);

        const LegacyDeckReader::Result passwordWrong = reader.readDeck(passwordOnlyPath, QStringLiteral("Wrong01"));
        QVERIFY(!passwordWrong.ok());
        QVERIFY(passwordWrong.legacyPasswordProtected);
        QVERIFY(passwordWrong.passwordRejected);
        QVERIFY(!passwordWrong.legacyPasswordVerified);
        QVERIFY(!passwordWrong.legacyPasswordVerificationUnavailable);

        QTemporaryDir readAccessDirectory;
        QVERIFY(readAccessDirectory.isValid());
        const QString readAccessPath = readAccessDirectory.filePath(QStringLiteral("readflag.BTN"));
        QVERIFY(QFile::copy(passwordOnlyPath, readAccessPath));
        QFile readAccessFile(readAccessPath);
        QVERIFY(readAccessFile.open(QIODevice::ReadWrite));
        QVERIFY(readAccessFile.seek(OldFormatOwnerFlagsOffset));
        const QByteArray ownerFlags = readAccessFile.read(1);
        QCOMPARE(ownerFlags.size(), 1);
        QVERIFY(readAccessFile.seek(OldFormatOwnerFlagsOffset));
        QCOMPARE(
            readAccessFile.write(QByteArray(1, static_cast<char>(
                static_cast<quint8>(ownerFlags.at(0)) | OldFormatOwnerReadAccessFlag))),
            qsizetype(1));
        readAccessFile.close();

        const LegacyDeckReader::Result readAllowedWithoutPassword = reader.readDeck(readAccessPath);
        QVERIFY2(readAllowedWithoutPassword.ok(), qPrintable(readAllowedWithoutPassword.errorMessage));
        QVERIFY(readAllowedWithoutPassword.legacyPasswordProtected);
        QVERIFY(!readAllowedWithoutPassword.passwordRequired);
        QVERIFY(!readAllowedWithoutPassword.passwordRejected);
        QVERIFY(!readAllowedWithoutPassword.legacyPasswordVerified);
        QVERIFY(readAllowedWithoutPassword.legacyPasswordVerificationUnavailable);
        QCOMPARE(readAllowedWithoutPassword.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(readAllowedWithoutPassword.deck.cardCount(), 3);

        const LegacyDeckReader::Result readAllowedWithWrongPassword = reader.readDeck(readAccessPath, QStringLiteral("Wrong01"));
        QVERIFY2(readAllowedWithWrongPassword.ok(), qPrintable(readAllowedWithWrongPassword.errorMessage));
        QVERIFY(readAllowedWithWrongPassword.legacyPasswordProtected);
        QVERIFY(!readAllowedWithWrongPassword.passwordRejected);
        QVERIFY(!readAllowedWithWrongPassword.legacyPasswordVerified);
        QVERIFY(readAllowedWithWrongPassword.legacyPasswordVerificationUnavailable);

        const LegacyDeckReader::Result encryptedMissing = reader.readDeck(encryptedPath);
        QVERIFY(encryptedMissing.passwordRequired);
        QVERIFY(!encryptedMissing.passwordRejected);

        const LegacyDeckReader::Result encrypted = reader.readDeck(encryptedPath, QStringLiteral("Crypt01"));
        QVERIFY2(encrypted.ok(), qPrintable(encrypted.errorMessage));
        QVERIFY(encrypted.legacyPasswordProtected);
        QVERIFY(encrypted.legacyDataEncrypted);
        QVERIFY(encrypted.legacyPasswordVerified);
        QVERIFY(!encrypted.legacyPasswordVerificationUnavailable);
        QCOMPARE(encrypted.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(encrypted.deck.fieldCount(), 11);
        QCOMPARE(encrypted.deck.cardCount(), 3);
        QCOMPARE(encrypted.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));

        for (int card = 0; card < plain.deck.cardCount(); ++card) {
            for (int field = 0; field < plain.deck.fieldCount(); ++field) {
                QCOMPARE(passwordOnly.deck.cardAt(card).valueAt(field), plain.deck.cardAt(card).valueAt(field));
                QCOMPARE(encrypted.deck.cardAt(card).valueAt(field), plain.deck.cardAt(card).valueAt(field));
            }
        }

        const LegacyDeckReader::Result encryptedWrong = reader.readDeck(encryptedPath, QStringLiteral("Wrong01"));
        QVERIFY(!encryptedWrong.ok());
        QVERIFY(encryptedWrong.legacyPasswordProtected);
        QVERIFY(encryptedWrong.legacyDataEncrypted);
        QVERIFY(encryptedWrong.passwordRejected);
        QVERIFY(!encryptedWrong.legacyPasswordVerified);
        QVERIFY(!encryptedWrong.legacyPasswordVerificationUnavailable);

        const LegacyDeckReader::Result notesHeavy = reader.readDeck(notesHeavyPath);
        QVERIFY2(notesHeavy.ok(), qPrintable(notesHeavy.errorMessage));
        QCOMPARE(notesHeavy.deck.name(), QStringLiteral("Notes Heavy Library"));
        QCOMPARE(notesHeavy.deck.cardAt(0).valueAt(10), QStringLiteral("Line1\r\nLine2\r\nLegacy note edge"));
        QCOMPARE(notesHeavy.deck.cardAt(1).valueAt(10), QStringLiteral("Tabbed\tand CRLF\r\nnotes sample"));
        QCOMPARE(notesHeavy.deck.cardAt(2).valueAt(10), QStringLiteral("012345678901234567890123456789012345678"));

        const LegacyDeckReader::Result maxLengths = reader.readDeck(maxLengthsPath);
        QVERIFY2(maxLengths.ok(), qPrintable(maxLengths.errorMessage));
        QCOMPARE(maxLengths.deck.name(), QStringLiteral("Max Length Library"));
        QCOMPARE(maxLengths.deck.cardAt(0).valueAt(0), QStringLiteral("PRODUCT-MAX-123456789012345"));
        QCOMPARE(maxLengths.deck.cardAt(0).valueAt(10), QStringLiteral("NOTES-MAX-12345678901234567890123456789"));

        const LegacyDeckReader::Result manyFields = reader.readDeck(manyFieldsPath);
        QVERIFY2(manyFields.ok(), qPrintable(manyFields.errorMessage));
        QCOMPARE(manyFields.deck.name(), QStringLiteral("Many Fields Library"));
        QCOMPARE(manyFields.deck.fieldCount(), 14);
        QCOMPARE(manyFields.deck.fieldAt(11).name(), QStringLiteral("ExtraA"));
        QCOMPARE(manyFields.deck.fieldAt(12).name(), QStringLiteral("ExtraB"));
        QCOMPARE(manyFields.deck.fieldAt(13).name(), QStringLiteral("ExtraC"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(11), QStringLiteral("A1"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(12), QStringLiteral("B1"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(13), QStringLiteral("C1"));

        QString expectedOemPrefix;
        expectedOemPrefix.append(QChar(0x00e9));
        expectedOemPrefix.append(QChar(0x00e4));
        expectedOemPrefix.append(QChar(0x00f6));
        expectedOemPrefix.append(QChar(0x00fc));
        const LegacyDeckReader::Result oemText = LegacyDeckReader({}, LegacyTextEncoding::Dos437).readDeck(oemTextPath);
        QVERIFY2(oemText.ok(), qPrintable(oemText.errorMessage));
        QVERIFY(oemText.deck.name().startsWith(expectedOemPrefix));
        QVERIFY(oemText.deck.cardAt(0).valueAt(0).startsWith(expectedOemPrefix));

        const LegacyDeckReader::Result unusualIndex = reader.readDeck(unusualIndexPath);
        QVERIFY2(unusualIndex.ok(), qPrintable(unusualIndex.errorMessage));
        QCOMPARE(unusualIndex.deck.fieldCount(), plain.deck.fieldCount());
        QCOMPARE(unusualIndex.deck.cardCount(), plain.deck.cardCount());

        const LegacyDeckReader::Result damagedTruncated = reader.readDeck(damagedTruncatedPath);
        QVERIFY(!damagedTruncated.ok());
        QVERIFY(!damagedTruncated.errorMessage.trimmed().isEmpty());

        const LegacyDeckReader::Result securityCycle = reader.readDeck(securityCyclePath);
        QVERIFY2(securityCycle.ok(), qPrintable(securityCycle.errorMessage));
        QVERIFY(!securityCycle.passwordRequired);
        QVERIFY(!securityCycle.passwordRejected);
        QCOMPARE(securityCycle.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(securityCycle.deck.cardCount(), 3);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runLegacyDeckReaderTests(int argc, char** argv)
{
    LegacyDeckReaderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(LegacyDeckReaderTests)
#endif

#include "LegacyDeckReaderTests.moc"

