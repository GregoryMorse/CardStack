#include <QApplication>
#include <QString>
#include <QTest>
#include <QVector>

#include <cstdio>
#include <functional>

int runMainWindowActionTests(int argc, char** argv);
int runDeckMergeTests(int argc, char** argv);
int runDelimitedTextTests(int argc, char** argv);
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
        {"DeckMergeTests", runDeckMergeTests},
        {"DelimitedTextTests", runDelimitedTextTests},
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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_LOGGING_RULES", "qt.qpa.*=false");

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

    int failures = 0;
    const QVector<TestEntry> entries = testEntries();
    bool matchedSelectedTest = selectedTest.isEmpty();

    for (const TestEntry& entry : entries) {
        if (!selectedTest.isEmpty() && selectedTest != QString::fromLocal8Bit(entry.name)) {
            continue;
        }

        matchedSelectedTest = true;
        failures += entry.run(forwardedArgc, forwardedArgv);
    }

    if (!matchedSelectedTest) {
        qCritical("Unknown test: %s", qPrintable(selectedTest));
        printUsage(argv[0]);
        return 2;
    }

    return failures == 0 ? 0 : 1;
}
