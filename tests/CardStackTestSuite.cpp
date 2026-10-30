#include <QApplication>
#include "ApplicationAppearance.h"
#include "DocumentFile.h"
#include "BtrieveFileSaverReader.h"
#include "LegacyInterchangeReader.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QEvent>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>
#include <QWidget>

#include <cstdio>
#include <QLockFile>
#include <functional>

#include "support/VisualTestSupport.h"

int runImportSecurityTests(int argc, char** argv);
int runFocusedAuditRegressionTests(int argc, char** argv);
int runMainWindowActionTests(int argc, char** argv);
int runMainWindowWorkflowTests(int argc, char** argv);
int runDeckMergeTests(int argc, char** argv);
int runDelimitedTextTests(int argc, char** argv);
int runPhoneCallLogTests(int argc, char** argv);
int runDeckTemplateTests(int argc, char** argv);
int runLegacyDeckReaderTests(int argc, char** argv);
int runLegacyInterchangeReaderTests(int argc, char** argv);
int runLegacyReportReaderTests(int argc, char** argv);
int runBtrieveAuditReaderTests(int argc, char** argv);
int runSQLiteDeckStoreTests(int argc, char** argv);
int runSQLitePackageStoreTests(int argc, char** argv);
int runPhoneticSearchTests(int argc, char** argv);
int runDeckWorkspaceTests(int argc, char** argv);
int runReportPreviewRendererTests(int argc, char** argv);
int runUiBuilderTests(int argc, char** argv);
int runReportDesignerWidgetTests(int argc, char** argv);
int runTemplateDesignerWidgetTests(int argc, char** argv);

namespace {

struct TestEntry {
    const char* name = nullptr;
    int (*run)(int argc, char** argv) = nullptr;
};

QVector<TestEntry> testEntries()
{
    return {
        {"MainWindowActionTests", runMainWindowActionTests},
        {"MainWindowWorkflowTests", runMainWindowWorkflowTests},
        {"FocusedAuditRegressionTests", runFocusedAuditRegressionTests},
        {"ImportSecurityTests", runImportSecurityTests},
        {"DeckMergeTests", runDeckMergeTests},
        {"DelimitedTextTests", runDelimitedTextTests},
        {"PhoneCallLogTests", runPhoneCallLogTests},
        {"DeckTemplateTests", runDeckTemplateTests},
        {"LegacyDeckReaderTests", runLegacyDeckReaderTests},
        {"LegacyInterchangeReaderTests", runLegacyInterchangeReaderTests},
        {"LegacyReportReaderTests", runLegacyReportReaderTests},
        {"BtrieveAuditReaderTests", runBtrieveAuditReaderTests},
        {"SQLiteDeckStoreTests", runSQLiteDeckStoreTests},
        {"SQLitePackageStoreTests", runSQLitePackageStoreTests},
        {"PhoneticSearchTests", runPhoneticSearchTests},
        {"DeckWorkspaceTests", runDeckWorkspaceTests},
        {"ReportPreviewRendererTests", runReportPreviewRendererTests},
        {"UiBuilderTests", runUiBuilderTests},
        {"ReportDesignerWidgetTests", runReportDesignerWidgetTests},
        {"TemplateDesignerWidgetTests", runTemplateDesignerWidgetTests},
    };
}

void printUsage(const char* executableName)
{
    std::printf("Usage: %s [--list] [--test TestName] [QtTest options]\n", executableName);
    std::printf("Available tests:\n");
    for (const TestEntry& entry : testEntries()) {
        std::printf("  %s\n", entry.name);
    }
}

bool stringEquals(const char* left, const char* right)
{
    return QString::fromLocal8Bit(left) == QString::fromLocal8Bit(right);
}

void cleanupTopLevelWidgets()
{
    const QList<QWidget*> widgets = QApplication::topLevelWidgets();
    for (QWidget* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        widget->close();
        widget->deleteLater();
    }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_LOGGING_RULES", "qt.qpa.*=false");
    QByteArray requestedPlatform = qgetenv("QT_QPA_PLATFORM");
    if (requestedPlatform.isEmpty()) {
        requestedPlatform = "offscreen";
        qputenv("QT_QPA_PLATFORM", requestedPlatform);
    }
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == "-platform" && index + 1 < argc) requestedPlatform = argv[index + 1];
        else if (QByteArray(argv[index]).startsWith("-platform=")) requestedPlatform = QByteArray(argv[index]).mid(10);
    }
#ifdef Q_OS_WIN
    if (requestedPlatform != "offscreen" && requestedPlatform != "minimal") {
        wchar_t name[512] = {};
        DWORD needed = 0;
        const auto desktop = GetThreadDesktop(GetCurrentThreadId());
        const QString expected = qEnvironmentVariable("CARDSTACK_TEST_ISOLATED_DESKTOP");
        const bool named = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed);
        if (!named || !expected.startsWith(QStringLiteral("CardStackOracle_")) || QString::fromWCharArray(name) != expected) {
            std::fputs("Native GUI tests require the private-desktop runner. Default test launches use the offscreen platform.\n", stderr);
            return 2;
        }
    }
#endif
    if (argc == 3 && (QString::fromLocal8Bit(argv[1]) == QLatin1String("--security-interchange")
                     || QString::fromLocal8Bit(argv[1]) == QLatin1String("--security-native"))) {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
        QCoreApplication core(argc, argv);
        CardStack::ImportLimits limits;
        limits.maximumInputBytes = CardStack::ImportMebibyte;
        limits.maximumDecodedBytes = CardStack::ImportMebibyte;
        limits.maximumWorkUnits = 2 * CardStack::ImportMebibyte;
        limits.maximumMilliseconds = 250;
        const QString path = QString::fromLocal8Bit(argv[2]);
        const bool ok = QString::fromLocal8Bit(argv[1]) == QLatin1String("--security-interchange")
            ? CardStack::LegacyInterchangeReader(limits).readFile(path).ok()
            : CardStack::BtrieveFileSaverReader(limits).readAllRecords(path).ok();
        return ok ? 0 : 7;
    }
    if (argc == 3 && (QString::fromLocal8Bit(argv[1]) == QLatin1String("--read-native-btrieve")
                     || QString::fromLocal8Bit(argv[1]) == QLatin1String("--unsafe-zero-page"))) {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
        QCoreApplication core(argc, argv);
        return CardStack::BtrieveFileSaverReader{}.readAllRecords(QString::fromLocal8Bit(argv[2])).ok() ? 0 : 7;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--hold-document-lock")) {
        QCoreApplication core(argc, argv);
        QLockFile lock(CardStack::documentLockPath(QString::fromLocal8Bit(argv[2])));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) return 1;
        std::fputs("locked\n", stdout);
        std::fflush(stdout);
        std::getchar();
        return 0;
    }

    QString selectedTest;
    QVector<char*> forwardedArgs;
    forwardedArgs.reserve(argc);
    forwardedArgs.append(argv[0]);

    for (int index = 1; index < argc; ++index) {
        if (stringEquals(argv[index], "--list")) {
            printUsage(argv[0]);
            return 0;
        }
        if (stringEquals(argv[index], "--test")) {
            if (index + 1 >= argc) {
                qCritical("Missing test name after --test.");
                printUsage(argv[0]);
                return 2;
            }
            selectedTest = QString::fromLocal8Bit(argv[++index]);
            continue;
        }
        forwardedArgs.append(argv[index]);
    }

    int forwardedArgc = forwardedArgs.size();
    char** forwardedArgv = forwardedArgs.data();
    QApplication app(forwardedArgc, forwardedArgv);
    QTemporaryDir testSettings;
    if (!testSettings.isValid()) {
        qCritical("Could not create isolated test settings directory.");
        return 2;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testSettings.path());
    CardStack::initializeApplicationAppearance(app);
    QCoreApplication::setOrganizationName(QStringLiteral("CardStackTests"));
    QCoreApplication::setApplicationName(QStringLiteral("CardStackTests"));
    CardStack::Tests::installVisualTestFonts();

    int failures = 0;
    const QVector<TestEntry> entries = testEntries();
    bool matchedSelectedTest = selectedTest.isEmpty();

    for (const TestEntry& entry : entries) {
        if (!selectedTest.isEmpty() && selectedTest != QString::fromLocal8Bit(entry.name)) {
            continue;
        }

        matchedSelectedTest = true;
        failures += entry.run(forwardedArgc, forwardedArgv);
        cleanupTopLevelWidgets();
    }

    if (!matchedSelectedTest) {
        qCritical("Unknown test: %s", qPrintable(selectedTest));
        printUsage(argv[0]);
        return 2;
    }

    cleanupTopLevelWidgets();
    return failures == 0 ? 0 : 1;
}
