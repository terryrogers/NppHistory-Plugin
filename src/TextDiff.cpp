#include "TextDiff.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <vector>

namespace npphistory
{
namespace
{
struct SourceLine
{
    std::wstring text;
    std::wstring ending;
    int number = 0;
};

std::vector<SourceLine> splitSourceLines(const std::wstring& text)
{
    std::vector<SourceLine> lines;
    std::size_t start = 0;
    int number = 1;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != L'\r' && text[index] != L'\n')
            continue;
        std::size_t endingLength = 1;
        if (text[index] == L'\r' && index + 1 < text.size() && text[index + 1] == L'\n')
            endingLength = 2;
        lines.push_back({text.substr(start, index - start), text.substr(index, endingLength), number++});
        index += endingLength - 1;
        start = index + 1;
    }
    if (start < text.size())
        lines.push_back({text.substr(start), L"", number});
    return lines;
}

std::vector<std::wstring> splitLines(const std::wstring& text)
{
    std::vector<std::wstring> result;
    for (const auto& line : splitSourceLines(text))
        result.push_back(line.text);
    return result;
}

bool isBlank(const std::wstring& text)
{
    return std::all_of(text.begin(), text.end(), [](wchar_t value) { return std::iswspace(value) != 0; });
}

std::wstring comparisonKey(const SourceLine& line, const CompareOptions& options)
{
    std::wstring key;
    key.reserve(line.text.size() + line.ending.size());
    for (const wchar_t value : line.text)
    {
        if (options.ignoreWhitespace && std::iswspace(value))
            continue;
        key.push_back(options.ignoreCase ? static_cast<wchar_t>(std::towlower(value)) : value);
    }
    if (!options.ignoreLineEndings)
        key += line.ending;
    return key;
}

std::pair<std::vector<DiffSpan>, std::vector<DiffSpan>> inlineDifferences(
    const std::wstring& left, const std::wstring& right)
{
    std::vector<DiffSpan> leftSpans;
    std::vector<DiffSpan> rightSpans;
    const std::size_t cells = (left.size() + 1) * (right.size() + 1);
    if (cells > 250'000)
    {
        std::size_t prefix = 0;
        while (prefix < left.size() && prefix < right.size() && left[prefix] == right[prefix])
            ++prefix;
        std::size_t suffix = 0;
        while (suffix < left.size() - prefix && suffix < right.size() - prefix
            && left[left.size() - suffix - 1] == right[right.size() - suffix - 1])
            ++suffix;
        if (left.size() > prefix + suffix)
            leftSpans.push_back({prefix, left.size() - prefix - suffix});
        if (right.size() > prefix + suffix)
            rightSpans.push_back({prefix, right.size() - prefix - suffix});
        return {leftSpans, rightSpans};
    }

    const std::size_t width = right.size() + 1;
    std::vector<unsigned int> lcs(cells, 0);
    for (std::size_t i = left.size(); i-- > 0;)
        for (std::size_t j = right.size(); j-- > 0;)
            lcs[i * width + j] = left[i] == right[j]
                ? 1 + lcs[(i + 1) * width + j + 1]
                : (std::max)(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);

    std::size_t i = 0;
    std::size_t j = 0;
    std::size_t leftStart = 0;
    std::size_t rightStart = 0;
    bool differing = false;
    const auto flush = [&]() {
        if (!differing)
            return;
        if (i > leftStart) leftSpans.push_back({leftStart, i - leftStart});
        if (j > rightStart) rightSpans.push_back({rightStart, j - rightStart});
        differing = false;
    };
    while (i < left.size() || j < right.size())
    {
        if (i < left.size() && j < right.size() && left[i] == right[j])
        {
            flush();
            ++i; ++j;
        }
        else
        {
            if (!differing)
            {
                leftStart = i;
                rightStart = j;
                differing = true;
            }
            if (j < right.size() && (i == left.size()
                || lcs[i * width + j + 1] >= lcs[(i + 1) * width + j]))
                ++j;
            else if (i < left.size())
                ++i;
        }
    }
    flush();
    return {leftSpans, rightSpans};
}
}

std::vector<DiffRow> makeSideBySideDiff(const std::wstring& current,
    const std::wstring& revision, const CompareOptions& options)
{
    const auto left = splitSourceLines(current);
    const auto right = splitSourceLines(revision);
    std::vector<std::wstring> leftKeys;
    std::vector<std::wstring> rightKeys;
    for (const auto& line : left) leftKeys.push_back(comparisonKey(line, options));
    for (const auto& line : right) rightKeys.push_back(comparisonKey(line, options));
    const std::size_t cells = (left.size() + 1) * (right.size() + 1);
    std::vector<DiffRow> rows;

    if (cells > 4'000'000)
    {
        const auto count = (std::max)(left.size(), right.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            const bool hasLeft = index < left.size();
            const bool hasRight = index < right.size();
            const bool equal = hasLeft && hasRight && leftKeys[index] == rightKeys[index];
            DiffRow row{hasLeft ? left[index].text : L"", hasRight ? right[index].text : L"",
                equal ? DiffKind::unchanged : (hasLeft && hasRight ? DiffKind::changed : hasLeft ? DiffKind::removed : DiffKind::empty),
                equal ? DiffKind::unchanged : (hasLeft && hasRight ? DiffKind::changed : hasRight ? DiffKind::added : DiffKind::empty),
                hasLeft ? left[index].number : 0, hasRight ? right[index].number : 0};
            if (!equal && hasLeft && hasRight)
            {
                auto spans = inlineDifferences(row.currentLine, row.revisionLine);
                row.currentSpans = std::move(spans.first);
                row.revisionSpans = std::move(spans.second);
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    const std::size_t width = right.size() + 1;
    std::vector<unsigned int> lcs(cells, 0);
    for (std::size_t i = left.size(); i-- > 0;)
        for (std::size_t j = right.size(); j-- > 0;)
            lcs[i * width + j] = leftKeys[i] == rightKeys[j]
                ? 1 + lcs[(i + 1) * width + j + 1]
                : (std::max)(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);

    std::vector<SourceLine> removed;
    std::vector<SourceLine> added;
    const auto flushChanges = [&]() {
        const auto count = (std::max)(removed.size(), added.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            const bool hasLeft = index < removed.size();
            const bool hasRight = index < added.size();
            DiffRow row{hasLeft ? removed[index].text : L"", hasRight ? added[index].text : L"",
                hasLeft && hasRight ? DiffKind::changed : hasLeft ? DiffKind::removed : DiffKind::empty,
                hasLeft && hasRight ? DiffKind::changed : hasRight ? DiffKind::added : DiffKind::empty,
                hasLeft ? removed[index].number : 0, hasRight ? added[index].number : 0};
            if (hasLeft && hasRight)
            {
                auto spans = inlineDifferences(row.currentLine, row.revisionLine);
                row.currentSpans = std::move(spans.first);
                row.revisionSpans = std::move(spans.second);
            }
            if (options.ignoreBlankLines)
            {
                if (hasLeft && !hasRight && isBlank(row.currentLine)) row.currentKind = DiffKind::unchanged;
                if (!hasLeft && hasRight && isBlank(row.revisionLine)) row.revisionKind = DiffKind::unchanged;
            }
            rows.push_back(std::move(row));
        }
        removed.clear();
        added.clear();
    };

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() || j < right.size())
    {
        if (i < left.size() && j < right.size() && leftKeys[i] == rightKeys[j])
        {
            flushChanges();
            rows.push_back({left[i].text, right[j].text, DiffKind::unchanged, DiffKind::unchanged,
                left[i].number, right[j].number});
            ++i;
            ++j;
        }
        else if (i + 1 < left.size() && j < right.size() && leftKeys[i + 1] == rightKeys[j])
        {
            // Prefer the immediately adjacent synchronization point. This is
            // especially important around repeated blank lines: an LCS tie can
            // otherwise pair an inserted line with a blank and mark later,
            // identical lines as changed.
            removed.push_back(left[i++]);
        }
        else if (i < left.size() && j + 1 < right.size() && leftKeys[i] == rightKeys[j + 1])
        {
            added.push_back(right[j++]);
        }
        else if (j < right.size() && (i == left.size()
            || lcs[i * width + j + 1] >= lcs[(i + 1) * width + j]))
        {
            added.push_back(right[j++]);
        }
        else if (i < left.size())
        {
            removed.push_back(left[i++]);
        }
    }
    flushChanges();
    return rows;
}

std::wstring makeUnifiedDiff(const std::wstring& older, const std::wstring& newer,
    const std::wstring& olderLabel, const std::wstring& newerLabel)
{
    const auto left = splitLines(older);
    const auto right = splitLines(newer);
    std::wostringstream output;
    output << L"--- " << olderLabel << L"\r\n+++ " << newerLabel << L"\r\n";

    const std::size_t cells = (left.size() + 1) * (right.size() + 1);
    if (cells > 4'000'000)
    {
        output << L"@@ File is too large for the beta LCS viewer; showing line-aligned differences. @@\r\n";
        const auto count = std::max(left.size(), right.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            if (index < left.size() && index < right.size() && left[index] == right[index])
                output << L"  " << left[index] << L"\r\n";
            else
            {
                if (index < left.size()) output << L"- " << left[index] << L"\r\n";
                if (index < right.size()) output << L"+ " << right[index] << L"\r\n";
            }
        }
        return output.str();
    }

    std::vector<unsigned int> lcs(cells, 0);
    const auto width = right.size() + 1;
    for (std::size_t i = left.size(); i-- > 0;)
    {
        for (std::size_t j = right.size(); j-- > 0;)
        {
            lcs[i * width + j] = left[i] == right[j]
                ? 1 + lcs[(i + 1) * width + j + 1]
                : std::max(lcs[(i + 1) * width + j], lcs[i * width + j + 1]);
        }
    }

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() || j < right.size())
    {
        if (i < left.size() && j < right.size() && left[i] == right[j])
        {
            output << L"  " << left[i] << L"\r\n";
            ++i;
            ++j;
        }
        else if (i + 1 < left.size() && j < right.size() && left[i + 1] == right[j])
        {
            output << L"- " << left[i++] << L"\r\n";
        }
        else if (i < left.size() && j + 1 < right.size() && left[i] == right[j + 1])
        {
            output << L"+ " << right[j++] << L"\r\n";
        }
        else if (j < right.size() && (i == left.size() || lcs[i * width + j + 1] >= lcs[(i + 1) * width + j]))
        {
            output << L"+ " << right[j++] << L"\r\n";
        }
        else if (i < left.size())
        {
            output << L"- " << left[i++] << L"\r\n";
        }
    }
    return output.str();
}
}
