#include "TestHarness.h"
#include "HistoryStore.h"
#include "Utilities.h"

#include <algorithm>

namespace fs = std::filesystem;
using namespace npphistory;

void runHistoryStoreTests(TestContext& context)
{
    TestDirectory directory(L"store");
    const std::vector<std::uint8_t> first{'o', 'n', 'e', '\n'};
    const std::vector<std::uint8_t> second{'o', 'n', 'e', '\n', 't', 'w', 'o', '\n'};
    const fs::path note = directory.path() / L"notes" / L"note.txt";
    context.expect(writeAllBytesAtomic(note, first), "history fixture is written");

    HistoryStore unconfigured;
    context.expect(!unconfigured.captureBytes(note, first, L"No root"),
        "HistoryStore::captureBytes rejects an unconfigured store");
    context.expect(!unconfigured.captureFile(directory.path() / L"missing.txt", L"Missing"),
        "HistoryStore::captureFile rejects a missing source file");

    HistoryStore store;
    const fs::path root = directory.path() / L"history";
    store.setRoot(root);
    context.expect(store.root() == root && fs::is_directory(root),
        "HistoryStore::setRoot creates and exposes the standalone root");
    const fs::path expectedBucket = root / utf8ToWide(sha256Hex(normalizePath(note)));
    context.expect(store.bucketFor(note) == expectedBucket,
        "HistoryStore::bucketFor deterministically hashes a normalized source path");
    context.expect(store.bucketFor(note) != store.bucketFor(directory.path() / L"other.txt"),
        "HistoryStore::bucketFor separates different source files");

    context.expect(store.captureFile(note, L"Before save"),
        "HistoryStore::captureFile records an existing file");
    context.expect(!store.captureFile(note, L"Duplicate"),
        "HistoryStore suppresses an adjacent duplicate revision");
    context.expect(store.captureFile(note, L"Forced", true),
        "HistoryStore force capture retains identical content");
    context.expect(writeAllBytesAtomic(note, second) && store.captureFile(note, L"Saved"),
        "HistoryStore captures changed content");

    const fs::path bucket = store.bucketFor(note);
    const auto pathMarkerBytes = readAllBytes(bucket / L"path.txt");
    context.expect(fs::is_regular_file(bucket / L"path.txt")
        && utf8ToWide(std::string(pathMarkerBytes.begin(), pathMarkerBytes.end())) == normalizePath(note),
        "capture writes the normalized source path marker");
    const auto latestBytes = readAllBytes(bucket / L"latest.hash");
    const std::string latest(latestBytes.begin(), latestBytes.end());
    context.expect(latest == sha256Hex(second), "capture updates latest.hash");

    auto revisions = store.revisionsFor(note);
    context.expect(revisions.size() == 3,
        "HistoryStore::revisionsFor lists normal, forced and changed captures");
    context.expect(revisions.front().reason == L"Saved" && revisions.front().size == second.size()
        && revisions.front().hash == sha256Hex(second),
        "revisionsFor parses metadata and orders newest first");
    context.expect(!revisions.front().timestamp.empty() && revisions.front().timestamp != L"Unknown",
        "revisionsFor formats the revision file timestamp");
    context.expect(store.readRevision(revisions.front()) == second,
        "HistoryStore::readRevision returns the exact stored bytes");

    const fs::path restored = directory.path() / L"restored" / L"note.txt";
    context.expect(store.restoreRevision(revisions.back(), restored)
        && readAllBytes(restored) == first,
        "HistoryStore::restoreRevision atomically restores an older revision");
    RevisionInfo missingRevision;
    missingRevision.revisionPath = directory.path() / L"missing.rev";
    missingRevision.metadataPath = directory.path() / L"missing.meta";
    context.expect(!store.restoreRevision(missingRevision, restored),
        "restoreRevision rejects a missing revision instead of writing an empty file");
    context.expect(!store.updateComment(missingRevision, L"invalid"),
        "updateComment rejects a missing revision and metadata pair");
    context.expect(!store.deleteRevision(note, missingRevision),
        "deleteRevision rejects missing revision files");

    context.expect(store.updateComment(revisions.front(), L"Edited\r\ncomment \u03A9"),
        "HistoryStore::updateComment writes Unicode comments");
    revisions = store.revisionsFor(note);
    context.expect(revisions.front().reason == L"Edited  comment \u03A9",
        "updateComment sanitizes line breaks without changing other text");

    const fs::path orphanMetadata = bucket / L"99999999-orphan.meta";
    context.expect(writeAllBytesAtomic(orphanMetadata, {'r', 'e', 'a', 's', 'o', 'n', '=', 'x', '\n'}),
        "orphan metadata fixture is written");
    context.expect(store.revisionsFor(note).size() == 3,
        "revisionsFor ignores metadata without a matching revision file");

    const fs::path malformedRevision = bucket / L"00000000-malformed.rev";
    const fs::path malformedMetadata = bucket / L"00000000-malformed.meta";
    writeAllBytesAtomic(malformedRevision, {'x'});
    const std::string malformedText = "reason=Malformed\nhash=abc\nsize=not-a-number\nignored-line\n";
    writeAllBytesAtomic(malformedMetadata,
        std::vector<std::uint8_t>(malformedText.begin(), malformedText.end()));
    const auto withMalformed = store.revisionsFor(note);
    const auto malformed = std::find_if(withMalformed.begin(), withMalformed.end(),
        [](const RevisionInfo& revision) { return revision.reason == L"Malformed"; });
    context.expect(malformed != withMalformed.end() && malformed->size == 0,
        "revisionsFor tolerates malformed size and unrelated metadata lines");

    const fs::path emptySource = directory.path() / L"empty.txt";
    context.expect(store.captureBytes(emptySource, {}, L"Empty"),
        "captureBytes supports an empty revision without requiring a source file");
    const auto emptyRevisions = store.revisionsFor(emptySource);
    context.expect(emptyRevisions.size() == 1 && store.readRevision(emptyRevisions.front()).empty(),
        "an empty revision is listed and read correctly");

    const fs::path deleteSource = directory.path() / L"delete.txt";
    HistoryStore deletionStore;
    deletionStore.setRoot(directory.path() / L"delete-history");
    context.expect(deletionStore.captureBytes(deleteSource, first, L"One")
        && deletionStore.captureBytes(deleteSource, second, L"Two"),
        "deletion fixture contains two revisions");
    auto deletionRevisions = deletionStore.revisionsFor(deleteSource);
    context.expect(deletionStore.deleteRevision(deleteSource, deletionRevisions.front()),
        "deleteRevision removes the selected newest revision");
    deletionRevisions = deletionStore.revisionsFor(deleteSource);
    const auto remainingHashBytes = readAllBytes(
        deletionStore.bucketFor(deleteSource) / L"latest.hash");
    const std::string remainingHash(remainingHashBytes.begin(), remainingHashBytes.end());
    context.expect(deletionRevisions.size() == 1 && remainingHash == deletionRevisions.front().hash,
        "deleteRevision recalculates latest.hash from the newest remaining revision");
    context.expect(deletionStore.deleteRevision(deleteSource, deletionRevisions.front()),
        "deleteRevision removes the final revision");
    context.expect(deletionStore.revisionsFor(deleteSource).empty()
        && !fs::exists(deletionStore.bucketFor(deleteSource) / L"latest.hash"),
        "deleteRevision removes latest.hash when no revisions remain");

    store.setCatalog(nullptr);
    context.expect(store.bucketFor(note) == expectedBucket,
        "HistoryStore::setCatalog accepts null and returns to standalone storage");
}
