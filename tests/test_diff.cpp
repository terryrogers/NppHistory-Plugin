#include "TestHarness.h"
#include "TextDiff.h"

#include <algorithm>

using namespace npphistory;

namespace
{
bool allUnchanged(const std::vector<DiffRow>& rows)
{
    return std::all_of(rows.begin(), rows.end(), [](const DiffRow& row) {
        return row.currentKind == DiffKind::unchanged
            && row.revisionKind == DiffKind::unchanged;
    });
}
}

void runDiffTests(TestContext& context)
{
    context.expect(makeSideBySideDiff(L"", L"").empty(),
        "makeSideBySideDiff returns no rows for two empty documents");
    const auto identical = makeSideBySideDiff(L"one\r\ntwo\r\n", L"one\r\ntwo\r\n");
    context.expect(identical.size() == 2 && allUnchanged(identical),
        "makeSideBySideDiff marks identical documents unchanged");
    context.expect(identical[0].currentLineNumber == 1 && identical[1].currentLineNumber == 2,
        "makeSideBySideDiff preserves original line numbers");

    const auto changed = makeSideBySideDiff(L"same\nold\nleft only\n",
        L"same\nnew\nright only\n");
    context.expect(changed.size() == 3
        && changed[1].currentKind == DiffKind::changed
        && changed[1].revisionKind == DiffKind::changed,
        "makeSideBySideDiff pairs replacement lines as changed");
    context.expect(!changed[1].currentSpans.empty() && !changed[1].revisionSpans.empty(),
        "paired changes include exact character spans");

    const auto removed = makeSideBySideDiff(L"one\nremoved\ntwo\n", L"one\ntwo\n");
    context.expect(std::any_of(removed.begin(), removed.end(), [](const DiffRow& row) {
        return row.currentLine == L"removed" && row.currentKind == DiffKind::removed
            && row.revisionKind == DiffKind::empty;
    }), "makeSideBySideDiff represents a current-only line as removed");
    const auto added = makeSideBySideDiff(L"one\ntwo\n", L"one\nadded\ntwo\n");
    context.expect(std::any_of(added.begin(), added.end(), [](const DiffRow& row) {
        return row.revisionLine == L"added" && row.currentKind == DiffKind::empty
            && row.revisionKind == DiffKind::added;
    }), "makeSideBySideDiff represents a revision-only line as added");

    CompareOptions whitespace;
    whitespace.ignoreWhitespace = true;
    context.expect(allUnchanged(makeSideBySideDiff(L"value = one\n", L"value=one\n", whitespace)),
        "ignoreWhitespace suppresses whitespace-only differences");
    CompareOptions letterCase;
    letterCase.ignoreCase = true;
    context.expect(allUnchanged(makeSideBySideDiff(L"Mixed Case\n", L"mixed case\n", letterCase)),
        "ignoreCase suppresses case-only differences");
    context.expect(!allUnchanged(makeSideBySideDiff(L"same\r\n", L"same\n")),
        "line-ending differences are detected by default");
    CompareOptions lineEndings;
    lineEndings.ignoreLineEndings = true;
    context.expect(allUnchanged(makeSideBySideDiff(L"same\r\n", L"same\n", lineEndings)),
        "ignoreLineEndings suppresses CRLF/LF differences");

    CompareOptions blankLines;
    blankLines.ignoreBlankLines = true;
    const auto removedBlank = makeSideBySideDiff(L"first\n \nlast\n", L"first\nlast\n", blankLines);
    context.expect(std::any_of(removedBlank.begin(), removedBlank.end(), [](const DiffRow& row) {
        return row.currentLineNumber == 2 && row.currentKind == DiffKind::unchanged;
    }), "ignoreBlankLines suppresses a removed whitespace-only line");
    const auto addedBlank = makeSideBySideDiff(L"first\nlast\n", L"first\n\t\nlast\n", blankLines);
    context.expect(std::any_of(addedBlank.begin(), addedBlank.end(), [](const DiffRow& row) {
        return row.revisionLineNumber == 2 && row.revisionKind == DiffKind::unchanged;
    }), "ignoreBlankLines suppresses an added whitespace-only line");

    const auto repeatedBlanks = makeSideBySideDiff(
        L"before\n\ninserted\n\n\nheading\nafter\n",
        L"before\n\n\n\nheading\nafter\n");
    context.expect(std::count_if(repeatedBlanks.begin(), repeatedBlanks.end(), [](const DiffRow& row) {
        return row.currentLine == L"inserted" && row.currentKind == DiffKind::removed;
    }) == 1, "LCS tie handling isolates an insertion beside repeated blank lines");
    context.expect(std::any_of(repeatedBlanks.begin(), repeatedBlanks.end(), [](const DiffRow& row) {
        return row.currentLine == L"heading" && row.revisionLine == L"heading"
            && row.currentKind == DiffKind::unchanged;
    }), "LCS tie handling keeps later identical lines aligned");

    const std::wstring longLeft = std::wstring(250, L'a') + L"LEFT" + std::wstring(250, L'z');
    const std::wstring longRight = std::wstring(250, L'a') + L"RIGHT" + std::wstring(250, L'z');
    const auto longInline = makeSideBySideDiff(longLeft, longRight);
    context.expect(longInline.size() == 1 && longInline[0].currentSpans.size() == 1
        && longInline[0].revisionSpans.size() == 1
        && longInline[0].currentSpans[0].start == 250
        && longInline[0].currentSpans[0].length == 3
        && longInline[0].revisionSpans[0].length == 4,
        "inline difference fallback finds the changed middle of a very long line");

    std::wstring largeLeft;
    std::wstring largeRight;
    for (int index = 0; index < 2'000; ++index)
    {
        largeLeft += L"left " + std::to_wstring(index) + L"\n";
        largeRight += (index == 1'000 ? L"changed\n"
            : L"left " + std::to_wstring(index) + L"\n");
    }
    const auto largeRows = makeSideBySideDiff(largeLeft, largeRight);
    context.expect(largeRows.size() == 2'000
        && largeRows[1'000].currentKind == DiffKind::changed,
        "makeSideBySideDiff uses its bounded line-aligned fallback for large documents");

    const std::wstring unified = makeUnifiedDiff(L"one\ntwo\n", L"one\nchanged\n", L"old", L"new");
    context.expect(unified.rfind(L"--- old\r\n+++ new\r\n", 0) == 0,
        "makeUnifiedDiff writes both labels");
    context.expect(unified.find(L"- two") != std::wstring::npos
        && unified.find(L"+ changed") != std::wstring::npos,
        "makeUnifiedDiff emits deletion and addition lines");
    const std::wstring largeUnified = makeUnifiedDiff(largeLeft, largeRight, L"old", L"new");
    context.expect(largeUnified.find(L"File is too large for the beta LCS viewer")
        != std::wstring::npos,
        "makeUnifiedDiff uses its documented large-document fallback");
}
