#pragma once

#include <string>
#include <vector>

namespace npphistory
{
enum class DiffKind
{
    unchanged,
    removed,
    added,
    changed,
    empty
};

struct CompareOptions
{
    bool ignoreWhitespace = false;
    bool ignoreBlankLines = false;
    bool ignoreCase = false;
    bool ignoreLineEndings = false;
};

struct DiffSpan
{
    std::size_t start = 0;
    std::size_t length = 0;
};

struct DiffRow
{
    std::wstring currentLine;
    std::wstring revisionLine;
    DiffKind currentKind = DiffKind::unchanged;
    DiffKind revisionKind = DiffKind::unchanged;
    int currentLineNumber = 0;
    int revisionLineNumber = 0;
    std::vector<DiffSpan> currentSpans;
    std::vector<DiffSpan> revisionSpans;
};

std::vector<DiffRow> makeSideBySideDiff(const std::wstring& current,
    const std::wstring& revision, const CompareOptions& options = {});
std::wstring makeUnifiedDiff(const std::wstring& older, const std::wstring& newer,
    const std::wstring& olderLabel, const std::wstring& newerLabel);
}
