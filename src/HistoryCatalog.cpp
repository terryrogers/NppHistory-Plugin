#include "HistoryCatalog.h"
#include "Utilities.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace npphistory
{
namespace
{
std::string hexEncode(const std::wstring& value)
{
    const std::string bytes = wideToUtf8(value);
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

int hexValue(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::wstring hexDecode(const std::string& value)
{
    if ((value.size() % 2) != 0)
        return {};
    std::string bytes;
    bytes.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2)
    {
        const int high = hexValue(value[index]);
        const int low = hexValue(value[index + 1]);
        if (high < 0 || low < 0)
            return {};
        bytes.push_back(static_cast<char>((high << 4) | low));
    }
    return utf8ToWide(bytes);
}

std::string newId()
{
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return utcTimestampCompact();
    static constexpr char digits[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (const auto byte : bytes)
    {
        id.push_back(digits[byte >> 4]);
        id.push_back(digits[byte & 0x0F]);
    }
    return id;
}

bool samePath(const fs::path& left, const fs::path& right)
{
    return normalizePath(left) == normalizePath(right);
}

std::string hashFile(const fs::path& path)
{
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        return {};
    return sha256Hex(readAllBytes(path));
}

std::wstring storedPath(const fs::path& bucket)
{
    std::ifstream input(bucket / L"path.txt", std::ios::binary);
    if (!input) return {};
    std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return utf8ToWide(value);
}

bool equalFiles(const fs::path& left, const fs::path& right)
{
    std::error_code error;
    if (fs::file_size(left, error) != fs::file_size(right, error) || error) return false;
    try { return readAllBytes(left) == readAllBytes(right); }
    catch (...) { return false; }
}

bool copyPreservingTime(const fs::path& from, const fs::path& to, std::error_code& error)
{
    if (!fs::copy_file(from, to, fs::copy_options::none, error) || error) return false;
    const auto time = fs::last_write_time(from, error);
    if (!error) fs::last_write_time(to, time, error);
    if (error)
    {
        std::error_code cleanup;
        fs::remove(to, cleanup);
    }
    return !error;
}

void removeEmptyAdjacentRoot(const fs::path& bucket)
{
    if (bucket.parent_path().filename() != L".npphistory") return;
    std::error_code error;
    if (fs::is_empty(bucket.parent_path(), error)) fs::remove(bucket.parent_path(), error);
}

void rebuildBucketState(const fs::path& bucket, const fs::path& filePath)
{
    const std::string normalized = wideToUtf8(normalizePath(filePath));
    writeAllBytesAtomic(bucket / L"path.txt",
        std::vector<std::uint8_t>(normalized.begin(), normalized.end()));
    fs::path newest;
    std::error_code error;
    for (fs::directory_iterator iterator(bucket, error), end; iterator != end && !error;
        iterator.increment(error))
    {
        if (iterator->is_regular_file() && iterator->path().extension() == L".meta"
            && (newest.empty() || iterator->path().filename() > newest.filename()))
            newest = iterator->path();
    }
    std::string newestHash;
    if (!newest.empty())
    {
        std::ifstream metadata(newest, std::ios::binary);
        std::string line;
        while (std::getline(metadata, line))
            if (line.rfind("hash=", 0) == 0) { newestHash = line.substr(5); break; }
    }
    if (!newestHash.empty())
        writeAllBytesAtomic(bucket / L"latest.hash",
            std::vector<std::uint8_t>(newestHash.begin(), newestHash.end()));
    else fs::remove(bucket / L"latest.hash", error);
}
}

void HistoryCatalog::configure(fs::path databaseFile, HistoryLocationMode mode, fs::path customRoot,
    fs::path legacyRoot)
{
    _databaseFile = std::move(databaseFile);
    _mode = mode;
    _customRoot = std::move(customRoot);
    _legacyRoot = std::move(legacyRoot);
    load();
}

void HistoryCatalog::load()
{
    _records.clear();
    std::ifstream input(_databaseFile, std::ios::binary);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true)
        {
            const auto separator = line.find('\t', start);
            fields.push_back(line.substr(start, separator == std::string::npos ? separator : separator - start));
            if (separator == std::string::npos) break;
            start = separator + 1;
        }
        if (fields.size() != 5 || fields[0].empty())
            continue;
        CatalogRecord record;
        record.id = fields[0];
        record.filePath = hexDecode(fields[1]);
        record.historyPath = hexDecode(fields[2]);
        record.lastHash = fields[3];
        record.hasHistory = fields[4] == "1";
        _records.push_back(std::move(record));
    }
}

bool HistoryCatalog::save() const
{
    std::ostringstream output;
    output << "# NppHistory catalog v1\n";
    for (const auto& record : _records)
    {
        output << record.id << '\t' << hexEncode(normalizePath(record.filePath)) << '\t'
            << hexEncode(record.historyPath.wstring()) << '\t' << record.lastHash << '\t'
            << (record.hasHistory ? '1' : '0') << '\n';
    }
    const std::string text = output.str();
    return writeAllBytesAtomic(_databaseFile,
        std::vector<std::uint8_t>(text.begin(), text.end()));
}

fs::path HistoryCatalog::desiredHistoryPath(const CatalogRecord& record, const fs::path& filePath) const
{
    if (_mode == HistoryLocationMode::customRoot && !_customRoot.empty())
        return _customRoot / utf8ToWide(record.id);
    return filePath.parent_path() / L".npphistory" / utf8ToWide(record.id);
}

void HistoryCatalog::ensureHiddenAdjacentRoot(const fs::path& historyPath)
{
    const fs::path root = historyPath.parent_path();
    std::error_code error;
    fs::create_directories(root, error);
    if (error)
        return;
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        SetFileAttributesW(root.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
}

bool HistoryCatalog::moveHistory(const fs::path& from, const fs::path& to)
{
    if (samePath(from, to))
        return true;
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    error.clear();
    fs::rename(from, to, error);
    if (!error)
    {
        if (from.parent_path().filename() == L".npphistory")
        {
            std::error_code cleanupError;
            if (fs::is_empty(from.parent_path(), cleanupError))
                fs::remove(from.parent_path(), cleanupError);
        }
        return true;
    }

    error.clear();
    fs::create_directories(to, error);
    if (error)
        return false;
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error)
        return false;

    const auto countFiles = [](const fs::path& root) {
        std::uintmax_t count = 0;
        std::error_code iterationError;
        for (fs::recursive_directory_iterator iterator(root, iterationError), end; iterator != end && !iterationError; iterator.increment(iterationError))
            if (iterator->is_regular_file()) ++count;
        return std::pair{count, iterationError};
    };
    const auto sourceCount = countFiles(from);
    const auto targetCount = countFiles(to);
    if (sourceCount.second || targetCount.second || targetCount.first < sourceCount.first)
        return false;
    fs::remove_all(from, error);
    if (!error && from.parent_path().filename() == L".npphistory")
    {
        std::error_code cleanupError;
        if (fs::is_empty(from.parent_path(), cleanupError))
            fs::remove(from.parent_path(), cleanupError);
    }
    return !error;
}

bool HistoryCatalog::mergeHistory(const fs::path& from, const fs::path& to,
    const fs::path& filePath, std::size_t& revisionCount)
{
    revisionCount = 0;
    if (samePath(from, to)) return true;
    std::error_code error;
    if (!fs::is_directory(to, error))
    {
        if (!moveHistory(from, to)) return false;
        for (fs::directory_iterator iterator(to, error), end; iterator != end && !error;
            iterator.increment(error))
            if (iterator->is_regular_file() && iterator->path().extension() == L".rev")
                ++revisionCount;
        rebuildBucketState(to, filePath);
        return true;
    }

    struct Copy { fs::path source; fs::path target; };
    std::vector<Copy> copies;
    std::set<fs::path> consumed;
    std::vector<fs::path> sourceRevisions;
    for (fs::directory_iterator iterator(from, error), end; iterator != end && !error;
        iterator.increment(error))
    {
        if (!iterator->is_regular_file()) return false; // Never discard an unknown nested item.
        if (iterator->path().extension() == L".rev") sourceRevisions.push_back(iterator->path());
    }
    if (error) return false;

    for (const auto& sourceRevision : sourceRevisions)
    {
        fs::path sourceMetadata = sourceRevision;
        sourceMetadata.replace_extension(L".meta");
        fs::path targetRevision = to / sourceRevision.filename();
        fs::path targetMetadata = to / sourceMetadata.filename();
        const bool metadataExists = fs::is_regular_file(sourceMetadata, error);
        const auto pairMatches = [&]() {
            return fs::is_regular_file(targetRevision, error)
                && equalFiles(sourceRevision, targetRevision)
                && (!metadataExists || (fs::is_regular_file(targetMetadata, error)
                    && equalFiles(sourceMetadata, targetMetadata)));
        };
        bool identical = pairMatches();
        if (!identical && (fs::exists(targetRevision, error)
            || (metadataExists && fs::exists(targetMetadata, error))))
        {
            const std::wstring stem = sourceRevision.stem().wstring();
            unsigned suffix = 1;
            while (true)
            {
                targetRevision = to / (stem + L"-migrated-" + std::to_wstring(suffix++) + L".rev");
                targetMetadata = targetRevision;
                targetMetadata.replace_extension(L".meta");
                if (pairMatches()) { identical = true; break; }
                if (!fs::exists(targetRevision, error) && !fs::exists(targetMetadata, error)) break;
            }
        }
        if (!identical)
        {
            copies.push_back({sourceRevision, targetRevision});
            if (metadataExists) copies.push_back({sourceMetadata, targetMetadata});
        }
        consumed.insert(sourceRevision);
        if (metadataExists) consumed.insert(sourceMetadata);
        ++revisionCount;
    }

    for (fs::directory_iterator iterator(from, error), end; iterator != end && !error;
        iterator.increment(error))
    {
        const fs::path source = iterator->path();
        if (!iterator->is_regular_file() || consumed.count(source)
            || source.filename() == L"path.txt" || source.filename() == L"latest.hash") continue;
        fs::path target = to / source.filename();
        if (fs::exists(target, error))
        {
            unsigned suffix = 1;
            const std::wstring stem = target.stem().wstring();
            const std::wstring extension = target.extension().wstring();
            do target = to / (stem + L"-migrated-" + std::to_wstring(suffix++) + extension);
            while (fs::exists(target, error));
        }
        copies.push_back({source, target});
    }
    if (error) return false;

    std::vector<fs::path> created;
    for (const auto& copy : copies)
    {
        if (!copyPreservingTime(copy.source, copy.target, error))
        {
            for (const auto& path : created) { std::error_code rollback; fs::remove(path, rollback); }
            return false;
        }
        created.push_back(copy.target);
    }
    fs::remove_all(from, error);
    if (error) return false;
    removeEmptyAdjacentRoot(from);
    rebuildBucketState(to, filePath);
    return true;
}

std::vector<fs::path> HistoryCatalog::adjacentHistoryPaths(const CatalogRecord& record,
    const fs::path& filePath, const fs::path& desired) const
{
    std::vector<fs::path> result;
    if (_mode != HistoryLocationMode::customRoot || filePath.empty()) return result;
    const fs::path root = filePath.parent_path() / L".npphistory";
    std::error_code error;
    if (!fs::is_directory(root, error)) return result;
    const fs::path hashed = root / utf8ToWide(sha256Hex(normalizePath(filePath)));
    const fs::path catalogued = root / utf8ToWide(record.id);
    const auto add = [&](const fs::path& candidate) {
        if (samePath(candidate, desired) || std::find_if(result.begin(), result.end(),
            [&](const fs::path& item) { return samePath(item, candidate); }) != result.end()) return;
        std::error_code candidateError;
        if (fs::is_directory(candidate, candidateError)) result.push_back(candidate);
    };
    add(hashed);
    add(catalogued);
    for (fs::directory_iterator iterator(root, error), end; iterator != end && !error;
        iterator.increment(error))
    {
        if (!iterator->is_directory()) continue;
        const std::wstring path = storedPath(iterator->path());
        if (!path.empty() && normalizePath(path) == normalizePath(filePath)) add(iterator->path());
    }
    return result;
}

ReconcileResult HistoryCatalog::reconcile(const fs::path& currentPath,
    const std::optional<fs::path>& knownPreviousPath)
{
    ReconcileResult result;
    if (currentPath.empty())
        return result;

    auto record = _records.end();
    if (knownPreviousPath && !knownPreviousPath->empty())
    {
        record = std::find_if(_records.begin(), _records.end(), [&](const CatalogRecord& item) {
            return samePath(item.filePath, *knownPreviousPath);
        });
    }
    if (record == _records.end())
    {
        record = std::find_if(_records.begin(), _records.end(), [&](const CatalogRecord& item) {
            return samePath(item.filePath, currentPath);
        });
    }

    if (record == _records.end())
    {
        const std::string currentHash = hashFile(currentPath);
        std::vector<std::size_t> candidates;
        if (!currentHash.empty())
        {
            for (std::size_t index = 0; index < _records.size(); ++index)
            {
                std::error_code error;
                if (_records[index].lastHash == currentHash && !fs::exists(_records[index].filePath, error))
                    candidates.push_back(index);
            }
        }
        if (candidates.size() == 1)
        {
            record = _records.begin() + static_cast<std::ptrdiff_t>(candidates.front());
            result.matchedByContent = true;
        }
        else if (candidates.size() > 1)
        {
            result.ambiguousMatch = true;
        }
    }

    if (record == _records.end())
    {
        CatalogRecord created;
        created.id = newId();
        created.filePath = currentPath;
        created.lastHash = hashFile(currentPath);
        created.historyPath = desiredHistoryPath(created, currentPath);
        if (!_legacyRoot.empty())
        {
            const fs::path legacy = _legacyRoot / utf8ToWide(sha256Hex(normalizePath(currentPath)));
            std::error_code legacyError;
            if (fs::is_directory(legacy, legacyError))
            {
                created.historyPath = legacy;
                created.hasHistory = true;
            }
        }
        _records.push_back(std::move(created));
        record = std::prev(_records.end());
        result.recordCreated = true;
    }

    const fs::path desired = desiredHistoryPath(*record, currentPath);
    result.previousHistoryPath = record->historyPath;
    if (!record->historyPath.empty() && !samePath(record->historyPath, desired))
    {
        std::error_code error;
        if (fs::is_directory(record->historyPath, error))
        {
            std::size_t moved = 0;
            const fs::path source = record->historyPath;
            result.historyMoved = mergeHistory(source, desired, currentPath, moved);
            if (!result.historyMoved)
            {
                result.moveFailed = true;
                if (_mode == HistoryLocationMode::customRoot
                    && source.parent_path().filename() == L".npphistory")
                {
                    result.adjacentMigrationFailed = true;
                    result.migrationSource = source;
                    result.migrationDestination = desired;
                }
                record->filePath = currentPath;
                result.historyPath = record->historyPath;
                save();
                return result;
            }
            if (_mode == HistoryLocationMode::customRoot
                && source.parent_path().filename() == L".npphistory")
            {
                result.adjacentHistoryMigrated = true;
                result.migratedRevisionCount += moved;
                ++result.migratedHistoryFolderCount;
                result.migrationSource = source;
                result.migrationDestination = desired;
                std::error_code rootError;
                result.adjacentRootRemoved = !fs::exists(source.parent_path(), rootError);
            }
        }
        else if (record->hasHistory)
        {
            result.historyMissing = true;
            record->hasHistory = false;
        }
    }
    else if (record->hasHistory)
    {
        std::error_code error;
        if (!fs::is_directory(desired, error))
        {
            result.historyMissing = true;
            record->hasHistory = false;
        }
    }

    if (_mode == HistoryLocationMode::customRoot)
    {
        for (const auto& source : adjacentHistoryPaths(*record, currentPath, desired))
        {
            std::size_t moved = 0;
            if (!mergeHistory(source, desired, currentPath, moved))
            {
                result.adjacentMigrationFailed = true;
                if (result.migrationSource.empty()) result.migrationSource = source;
                result.migrationDestination = desired;
                continue;
            }
            result.historyMoved = true;
            result.adjacentHistoryMigrated = true;
            result.migratedRevisionCount += moved;
            ++result.migratedHistoryFolderCount;
            if (result.migrationSource.empty()) result.migrationSource = source;
            result.migrationDestination = desired;
            record->hasHistory = record->hasHistory || moved > 0;
            std::error_code rootError;
            result.adjacentRootRemoved = !fs::exists(source.parent_path(), rootError);
        }
    }

    record->filePath = currentPath;
    record->historyPath = desired;
    if (_mode == HistoryLocationMode::adjacent)
        ensureHiddenAdjacentRoot(desired);
    else
    {
        std::error_code error;
        fs::create_directories(desired.parent_path(), error);
    }
    result.historyPath = desired;
    save();
    return result;
}

fs::path HistoryCatalog::historyPathFor(const fs::path& filePath) const
{
    const auto record = std::find_if(_records.begin(), _records.end(), [&](const CatalogRecord& item) {
        return samePath(item.filePath, filePath);
    });
    return record == _records.end() ? fs::path{} : record->historyPath;
}

void HistoryCatalog::recordCapture(const fs::path& filePath, const std::string& contentHash)
{
    const auto record = std::find_if(_records.begin(), _records.end(), [&](const CatalogRecord& item) {
        return samePath(item.filePath, filePath);
    });
    if (record == _records.end())
        return;
    record->lastHash = contentHash;
    record->hasHistory = true;
    save();
}
}
