#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace npphistory
{
enum class HistoryLocationMode
{
    adjacent,
    customRoot
};

struct CatalogRecord
{
    std::string id;
    std::filesystem::path filePath;
    std::filesystem::path historyPath;
    std::string lastHash;
    bool hasHistory = false;
};

struct ReconcileResult
{
    std::filesystem::path historyPath;
    std::filesystem::path previousHistoryPath;
    bool recordCreated = false;
    bool historyMoved = false;
    bool historyMissing = false;
    bool moveFailed = false;
    bool matchedByContent = false;
    bool ambiguousMatch = false;
    bool adjacentHistoryMigrated = false;
    bool adjacentMigrationFailed = false;
    bool adjacentRootRemoved = false;
    std::size_t migratedRevisionCount = 0;
    std::size_t migratedHistoryFolderCount = 0;
    std::filesystem::path migrationSource;
    std::filesystem::path migrationDestination;
};

class HistoryCatalog
{
public:
    void configure(std::filesystem::path databaseFile, HistoryLocationMode mode,
        std::filesystem::path customRoot, std::filesystem::path legacyRoot = {});
    ReconcileResult reconcile(const std::filesystem::path& currentPath,
        const std::optional<std::filesystem::path>& knownPreviousPath = std::nullopt);
    std::filesystem::path historyPathFor(const std::filesystem::path& filePath) const;
    void recordCapture(const std::filesystem::path& filePath, const std::string& contentHash);
    const std::filesystem::path& databaseFile() const noexcept { return _databaseFile; }
    const std::vector<CatalogRecord>& records() const noexcept { return _records; }

private:
    void load();
    bool save() const;
    std::filesystem::path desiredHistoryPath(const CatalogRecord& record,
        const std::filesystem::path& filePath) const;
    static bool moveHistory(const std::filesystem::path& from, const std::filesystem::path& to);
    static bool mergeHistory(const std::filesystem::path& from, const std::filesystem::path& to,
        const std::filesystem::path& filePath, std::size_t& revisionCount);
    std::vector<std::filesystem::path> adjacentHistoryPaths(
        const CatalogRecord& record, const std::filesystem::path& filePath,
        const std::filesystem::path& desired) const;
    static void ensureHiddenAdjacentRoot(const std::filesystem::path& historyPath);

    std::filesystem::path _databaseFile;
    HistoryLocationMode _mode = HistoryLocationMode::adjacent;
    std::filesystem::path _customRoot;
    std::filesystem::path _legacyRoot;
    std::vector<CatalogRecord> _records;
};
}
