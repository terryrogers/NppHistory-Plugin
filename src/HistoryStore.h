#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace npphistory
{
class HistoryCatalog;

struct RevisionInfo
{
    std::filesystem::path revisionPath;
    std::filesystem::path metadataPath;
    std::wstring timestamp;
    std::wstring reason;
    std::string hash;
    std::uintmax_t size = 0;
};

class HistoryStore
{
public:
    void setRoot(std::filesystem::path root);
    void setCatalog(HistoryCatalog* catalog);
    const std::filesystem::path& root() const noexcept;
    bool captureFile(const std::filesystem::path& sourcePath, const std::wstring& reason,
        bool force = false);
    bool captureBytes(const std::filesystem::path& sourcePath,
        const std::vector<std::uint8_t>& bytes, const std::wstring& reason, bool force = false);
    std::vector<RevisionInfo> revisionsFor(const std::filesystem::path& sourcePath) const;
    std::vector<std::uint8_t> readRevision(const RevisionInfo& revision) const;
    bool restoreRevision(const RevisionInfo& revision, const std::filesystem::path& destination) const;
    bool updateComment(const RevisionInfo& revision, const std::wstring& comment) const;
    bool deleteRevision(const std::filesystem::path& sourcePath,
        const RevisionInfo& revision) const;
    std::filesystem::path bucketFor(const std::filesystem::path& sourcePath) const;

private:
    std::filesystem::path _root;
    HistoryCatalog* _catalog = nullptr;
};
}
