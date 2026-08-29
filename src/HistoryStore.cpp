#include "HistoryStore.h"
#include "HistoryCatalog.h"
#include "Utilities.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace npphistory
{
void HistoryStore::setRoot(fs::path root)
{
    _root = std::move(root);
    std::error_code error;
    fs::create_directories(_root, error);
}

void HistoryStore::setCatalog(HistoryCatalog* catalog)
{
    _catalog = catalog;
}

const fs::path& HistoryStore::root() const noexcept
{
    return _root;
}

fs::path HistoryStore::bucketFor(const fs::path& sourcePath) const
{
    if (_catalog)
        return _catalog->historyPathFor(sourcePath);
    return _root / utf8ToWide(sha256Hex(normalizePath(sourcePath)));
}

bool HistoryStore::captureFile(const fs::path& sourcePath, const std::wstring& reason, bool force)
{
    std::error_code error;
    if (!fs::is_regular_file(sourcePath, error))
        return false;
    return captureBytes(sourcePath, readAllBytes(sourcePath), reason, force);
}

bool HistoryStore::captureBytes(const fs::path& sourcePath,
    const std::vector<std::uint8_t>& bytes, const std::wstring& reason, bool force)
{
    if (_catalog && _catalog->historyPathFor(sourcePath).empty())
        _catalog->reconcile(sourcePath);
    if (!_catalog && _root.empty())
        return false;

    const std::string contentHash = sha256Hex(bytes);
    const fs::path bucket = bucketFor(sourcePath);
    std::error_code error;
    fs::create_directories(bucket, error);
    if (error)
        return false;

    const fs::path latestHashPath = bucket / L"latest.hash";
    if (fs::exists(latestHashPath, error))
    {
        std::ifstream latest(latestHashPath, std::ios::binary);
        std::string previousHash;
        std::getline(latest, previousHash);
        if (!force && previousHash == contentHash)
        {
            if (_catalog)
                _catalog->recordCapture(sourcePath, contentHash);
            return false;
        }
    }

    const auto pathUtf8 = wideToUtf8(normalizePath(sourcePath));
    writeAllBytesAtomic(bucket / L"path.txt", std::vector<std::uint8_t>(pathUtf8.begin(), pathUtf8.end()));

    std::string stem = utcTimestampCompact() + "_" + contentHash.substr(0, 12);
    fs::path revisionPath = bucket / utf8ToWide(stem + ".rev");
    unsigned int suffix = 1;
    while (fs::exists(revisionPath, error))
    {
        revisionPath = bucket / utf8ToWide(stem + "-" + std::to_string(suffix++) + ".rev");
    }
    if (!writeAllBytesAtomic(revisionPath, bytes))
        return false;

    std::ostringstream metadata;
    metadata << "reason=" << wideToUtf8(reason) << "\n";
    metadata << "hash=" << contentHash << "\n";
    metadata << "size=" << bytes.size() << "\n";
    const auto metadataText = metadata.str();
    fs::path metadataPath = revisionPath;
    metadataPath.replace_extension(L".meta");
    if (!writeAllBytesAtomic(metadataPath, std::vector<std::uint8_t>(metadataText.begin(), metadataText.end())))
    {
        fs::remove(revisionPath, error);
        return false;
    }

    writeAllBytesAtomic(latestHashPath, std::vector<std::uint8_t>(contentHash.begin(), contentHash.end()));
    if (_catalog)
        _catalog->recordCapture(sourcePath, contentHash);
    return true;
}

std::vector<RevisionInfo> HistoryStore::revisionsFor(const fs::path& sourcePath) const
{
    std::vector<RevisionInfo> revisions;
    const fs::path bucket = bucketFor(sourcePath);
    std::error_code error;
    if (!fs::is_directory(bucket, error))
        return revisions;

    for (const auto& entry : fs::directory_iterator(bucket, error))
    {
        if (error || !entry.is_regular_file() || entry.path().extension() != L".meta")
            continue;

        RevisionInfo revision;
        revision.metadataPath = entry.path();
        revision.revisionPath = entry.path();
        revision.revisionPath.replace_extension(L".rev");
        if (!fs::exists(revision.revisionPath, error))
            continue;

        std::ifstream metadata(entry.path(), std::ios::binary);
        std::string line;
        while (std::getline(metadata, line))
        {
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "reason") revision.reason = utf8ToWide(value);
            else if (key == "hash") revision.hash = value;
            else if (key == "size")
            {
                try { revision.size = static_cast<std::uintmax_t>(std::stoull(value)); }
                catch (...) { revision.size = 0; }
            }
        }

        const auto modified = fs::last_write_time(revision.revisionPath, error);
        revision.timestamp = error ? L"Unknown" : localTimestampDisplay(modified);
        revisions.push_back(std::move(revision));
    }

    std::sort(revisions.begin(), revisions.end(), [](const RevisionInfo& left, const RevisionInfo& right) {
        return left.revisionPath.filename().wstring() > right.revisionPath.filename().wstring();
    });
    return revisions;
}

std::vector<std::uint8_t> HistoryStore::readRevision(const RevisionInfo& revision) const
{
    return readAllBytes(revision.revisionPath);
}

bool HistoryStore::restoreRevision(const RevisionInfo& revision, const fs::path& destination) const
{
    std::error_code error;
    if (!fs::is_regular_file(revision.revisionPath, error))
        return false;
    return writeAllBytesAtomic(destination, readRevision(revision));
}

bool HistoryStore::updateComment(const RevisionInfo& revision, const std::wstring& comment) const
{
    std::error_code error;
    if (!fs::is_regular_file(revision.revisionPath, error)
        || !fs::is_regular_file(revision.metadataPath, error))
        return false;
    std::wstring sanitized = comment;
    std::replace(sanitized.begin(), sanitized.end(), L'\r', L' ');
    std::replace(sanitized.begin(), sanitized.end(), L'\n', L' ');
    std::ostringstream metadata;
    metadata << "reason=" << wideToUtf8(sanitized) << "\n";
    metadata << "hash=" << revision.hash << "\n";
    metadata << "size=" << revision.size << "\n";
    const std::string text = metadata.str();
    return writeAllBytesAtomic(revision.metadataPath,
        std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool HistoryStore::deleteRevision(const fs::path& sourcePath,
    const RevisionInfo& revision) const
{
    std::error_code validationError;
    if (!fs::is_regular_file(revision.revisionPath, validationError)
        || !fs::is_regular_file(revision.metadataPath, validationError))
        return false;
    std::error_code revisionError;
    const bool revisionRemoved = fs::remove(revision.revisionPath, revisionError);
    std::error_code metadataError;
    const bool metadataRemoved = fs::remove(revision.metadataPath, metadataError);
    const bool filesRemoved = revisionRemoved && !revisionError
        && metadataRemoved && !metadataError;

    const fs::path latestHashPath = bucketFor(sourcePath) / L"latest.hash";
    const auto remaining = revisionsFor(sourcePath);
    if (remaining.empty())
    {
        std::error_code hashError;
        fs::remove(latestHashPath, hashError);
        return filesRemoved && !hashError;
    }
    const std::string newestHash = remaining.front().hash;
    const bool hashUpdated = writeAllBytesAtomic(latestHashPath,
        std::vector<std::uint8_t>(newestHash.begin(), newestHash.end()));
    return filesRemoved && hashUpdated;
}
}
