#include "TestHarness.h"
#include "HistoryCatalog.h"
#include "HistoryStore.h"
#include "Utilities.h"

#include <fstream>
#include <regex>

namespace fs = std::filesystem;
using namespace npphistory;

void runHistoryCatalogTests(TestContext& context)
{
    const std::vector<std::uint8_t> content{'c', 'a', 't', 'a', 'l', 'o', 'g'};

    TestDirectory basicDirectory(L"catalog-basic");
    const fs::path database = basicDirectory.path() / L"config" / L"catalog.db";
    const fs::path firstFolder = basicDirectory.path() / L"first";
    const fs::path secondFolder = basicDirectory.path() / L"second";
    const fs::path thirdFolder = basicDirectory.path() / L"third";
    const fs::path note = firstFolder / L"tracked.txt";
    writeAllBytesAtomic(note, content);

    HistoryCatalog catalog;
    catalog.configure(database, HistoryLocationMode::adjacent, {});
    context.expect(catalog.databaseFile() == database && catalog.records().empty(),
        "HistoryCatalog::configure stores its database path and loads an absent database");
    context.expect(catalog.reconcile({}).historyPath.empty() && catalog.records().empty(),
        "HistoryCatalog::reconcile ignores an empty path");
    context.expect(catalog.historyPathFor(note).empty(),
        "HistoryCatalog::historyPathFor returns empty for an unknown file");

    const ReconcileResult created = catalog.reconcile(note);
    context.expect(created.recordCreated && catalog.records().size() == 1,
        "reconcile creates exactly one catalogue record for a new file");
    context.expect(std::regex_match(catalog.records().front().id,
        std::regex(R"(^[0-9a-f]{32}$|^\d{8}-\d{6}-\d{3}$)")),
        "new catalogue IDs use random hex or the documented timestamp fallback");
    context.expect(created.historyPath.parent_path().filename() == L".npphistory",
        "adjacent mode selects a hidden .npphistory root");
    const DWORD attributes = GetFileAttributesW(created.historyPath.parent_path().c_str());
    context.expect(attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0,
        "adjacent reconciliation creates and hides the .npphistory root");
    context.expect(fs::is_regular_file(database),
        "reconcile persists the catalogue database atomically");
    const auto reconciledAgain = catalog.reconcile(note);
    context.expect(!reconciledAgain.recordCreated && catalog.records().size() == 1,
        "reconciling the same path reuses its existing record");

    const std::size_t recordCount = catalog.records().size();
    catalog.recordCapture(basicDirectory.path() / L"unknown.txt", "ignored");
    context.expect(catalog.records().size() == recordCount,
        "recordCapture ignores an unknown file path");

    HistoryStore store;
    store.setCatalog(&catalog);
    context.expect(store.bucketFor(note) == created.historyPath,
        "HistoryStore::setCatalog routes its bucket through the catalogue");
    context.expect(store.captureFile(note, L"Saved"),
        "catalogue-backed HistoryStore captures into the reconciled bucket");
    context.expect(catalog.records().front().hasHistory
        && catalog.records().front().lastHash == sha256Hex(content),
        "recordCapture persists the latest content hash and history flag");

    HistoryCatalog reloaded;
    reloaded.configure(database, HistoryLocationMode::adjacent, {});
    context.expect(reloaded.records().size() == 1
        && normalizePath(reloaded.records().front().filePath) == normalizePath(note)
        && reloaded.records().front().lastHash == sha256Hex(content)
        && reloaded.records().front().hasHistory,
        "HistoryCatalog::load reconstructs every persisted record field");
    {
        std::ofstream append(database, std::ios::binary | std::ios::app);
        append << "\n# comment\ninvalid\nmissing\tfields\n";
    }
    HistoryCatalog tolerant;
    tolerant.configure(database, HistoryLocationMode::adjacent, {});
    context.expect(tolerant.records().size() == 1,
        "HistoryCatalog::load skips comments, blank lines and malformed records");

    fs::create_directories(secondFolder);
    const fs::path movedNote = secondFolder / L"tracked.txt";
    std::error_code error;
    fs::rename(note, movedNote, error);
    const auto explicitlyMoved = catalog.reconcile(movedNote, note);
    context.expect(!error && explicitlyMoved.historyMoved
        && !fs::exists(created.historyPath),
        "reconcile moves history after an explicit file rename");
    context.expect(catalog.historyPathFor(movedNote) == explicitlyMoved.historyPath,
        "historyPathFor follows the explicitly moved file");

    fs::create_directories(thirdFolder);
    const fs::path reopenedNote = thirdFolder / L"tracked.txt";
    fs::rename(movedNote, reopenedNote, error);
    const auto contentMatched = catalog.reconcile(reopenedNote);
    context.expect(!error && contentMatched.matchedByContent && contentMatched.historyMoved,
        "reconcile identifies one externally moved file by its last captured hash");

    const fs::path customRoot = basicDirectory.path() / L"common-history";
    catalog.configure(database, HistoryLocationMode::customRoot, customRoot);
    const auto custom = catalog.reconcile(reopenedNote);
    context.expect(custom.historyMoved && custom.historyPath.parent_path() == customRoot,
        "reconcile migrates existing history into a configured common root");
    context.expect(fs::is_directory(customRoot),
        "custom-root reconciliation creates the configured parent directory");
    fs::remove_all(custom.historyPath, error);
    const auto missing = catalog.reconcile(reopenedNote);
    context.expect(missing.historyMissing && !catalog.records().front().hasHistory,
        "reconcile reports and clears a missing recorded history directory");

    TestDirectory ambiguousDirectory(L"catalog-ambiguous");
    HistoryCatalog ambiguousCatalog;
    ambiguousCatalog.configure(ambiguousDirectory.path() / L"catalog.db",
        HistoryLocationMode::adjacent, {});
    HistoryStore ambiguousStore;
    ambiguousStore.setCatalog(&ambiguousCatalog);
    const fs::path sameOne = ambiguousDirectory.path() / L"one" / L"same.txt";
    const fs::path sameTwo = ambiguousDirectory.path() / L"two" / L"same.txt";
    writeAllBytesAtomic(sameOne, content);
    writeAllBytesAtomic(sameTwo, content);
    ambiguousCatalog.reconcile(sameOne);
    ambiguousCatalog.reconcile(sameTwo);
    ambiguousStore.captureFile(sameOne, L"One");
    ambiguousStore.captureFile(sameTwo, L"Two");
    fs::remove(sameOne, error);
    fs::remove(sameTwo, error);
    const fs::path ambiguousNew = ambiguousDirectory.path() / L"three" / L"same.txt";
    writeAllBytesAtomic(ambiguousNew, content);
    const auto ambiguous = ambiguousCatalog.reconcile(ambiguousNew);
    context.expect(ambiguous.ambiguousMatch && ambiguous.recordCreated
        && ambiguousCatalog.records().size() == 3,
        "reconcile refuses to guess when multiple missing files share a hash");

    TestDirectory failedMoveDirectory(L"catalog-move-failure");
    const fs::path failedDatabase = failedMoveDirectory.path() / L"catalog.db";
    const fs::path failedNote = failedMoveDirectory.path() / L"note.txt";
    writeAllBytesAtomic(failedNote, content);
    HistoryCatalog failedCatalog;
    failedCatalog.configure(failedDatabase, HistoryLocationMode::adjacent, {});
    HistoryStore failedStore;
    failedStore.setCatalog(&failedCatalog);
    failedCatalog.reconcile(failedNote);
    failedStore.captureFile(failedNote, L"Saved");
    const fs::path previousHistory = failedCatalog.historyPathFor(failedNote);
    const fs::path blockedRoot = failedMoveDirectory.path() / L"blocked-file";
    context.expect(writeAllBytesAtomic(blockedRoot, {'b', 'l', 'o', 'c', 'k'}),
        "blocked migration fixture uses a file where a directory is required");
    failedCatalog.configure(failedDatabase, HistoryLocationMode::customRoot, blockedRoot);
    const auto failedMove = failedCatalog.reconcile(failedNote);
    context.expect(failedMove.moveFailed,
        "reconcile reports a blocked history migration");
    context.expect(failedMove.historyPath == previousHistory,
        "a blocked migration retains the previous catalogue history path");
    context.expect(fs::is_directory(previousHistory),
        "a blocked migration leaves the existing history directory intact");
    if (!failedMove.moveFailed || failedMove.historyPath != previousHistory
        || !fs::is_directory(previousHistory))
    {
        std::cerr << "  moveFailed=" << failedMove.moveFailed
            << " previous=" << wideToUtf8(previousHistory.wstring())
            << " result=" << wideToUtf8(failedMove.historyPath.wstring())
            << " previousExists=" << fs::exists(previousHistory)
            << " previousDirectory=" << fs::is_directory(previousHistory) << '\n';
    }

    TestDirectory legacyDirectory(L"catalog-legacy");
    const fs::path legacyNote = legacyDirectory.path() / L"note.txt";
    const fs::path legacyRoot = legacyDirectory.path() / L"legacy";
    writeAllBytesAtomic(legacyNote, content);
    const fs::path legacyBucket = legacyRoot
        / utf8ToWide(sha256Hex(normalizePath(legacyNote)));
    writeAllBytesAtomic(legacyBucket / L"legacy.rev", {'o', 'l', 'd'});
    HistoryCatalog legacyCatalog;
    legacyCatalog.configure(legacyDirectory.path() / L"catalog.db",
        HistoryLocationMode::adjacent, {}, legacyRoot);
    const auto legacy = legacyCatalog.reconcile(legacyNote);
    context.expect(legacy.recordCreated && legacy.historyMoved
        && fs::is_regular_file(legacy.historyPath / L"legacy.rev")
        && !fs::exists(legacyBucket),
        "reconcile discovers and migrates a legacy hash-addressed history bucket");
}
