#include "TestHarness.h"

int wmain()
{
    TestContext context;
    runUtilityTests(context);
    runSettingsTests(context);
    runDiffTests(context);
    runHistoryStoreTests(context);
    runHistoryCatalogTests(context);

    std::cout << "NppHistory core verification: " << context.checks << " checks, "
        << context.failures << " failures.\n";
    if (context.failures == 0)
        std::cout << "All NppHistory core tests passed.\n";
    return context.failures == 0 ? 0 : 1;
}
