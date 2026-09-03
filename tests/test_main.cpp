#include "TestHarness.h"
void runDocumentTabTests(TestContext& context);
void runTemporaryStatusTests(TestContext& context);
void runActionFeedbackTests(TestContext& context);

int wmain()
{
    TestContext context;
    runTemporaryStatusTests(context);
    runActionFeedbackTests(context);
    runDocumentTabTests(context);
    runUtilityTests(context);
    runSettingsTests(context);
    runDiffTests(context);
    runHistoryStoreTests(context);
    runHistoryCatalogTests(context);
    runUpdateCheckerTests(context);
    runLoggerTests(context);

    std::cout << "NppHistory core verification: " << context.checks << " checks, "
        << context.failures << " failures.\n";
    if (context.failures == 0)
        std::cout << "All NppHistory core tests passed.\n";
    return context.failures == 0 ? 0 : 1;
}
