#include "ActionFeedback.h"
#include "Logger.h"
#include "TemporaryStatusBar.h"

namespace npphistory
{
namespace
{
std::wstring singleLine(std::wstring text)
{
    for (auto& character : text)
        if (character < L' ' || character == 0x7F) character = L' ';
    return text;
}
}

ActionFeedback makeActionFeedback(ActionEvent event, const ActionContext& context)
{
    const std::wstring file = context.file.filename().empty()
        ? L"Current File" : singleLine(context.file.filename().wstring());
    const std::wstring revision = context.revision.empty()
        ? L"Selected" : singleLine(context.revision);
    const std::wstring check = context.automatic ? L"Automatic Update Check" : L"Manual Update Check";
    const std::wstring version = context.version.empty()
        ? L"" : L" (Version " + singleLine(context.version) + L")";
    const auto info = LogLevel::informational;
    const auto warning = LogLevel::warning;
    const auto critical = LogLevel::critical;
    switch (event)
    {
    case ActionEvent::captured: return {file + L" Revision Captured.", info};
    case ActionEvent::captureFailed: return {L"Revision Capture for " + file + L" Failed!", critical};
    case ActionEvent::refreshed: return {L"History for " + file + L" Refreshed.", info};
    case ActionEvent::comparisonOpened: return {file + L" Comparison View Opened.", info};
    case ActionEvent::comparisonFailed: return {L"Comparison View for " + file + L" Failed to Open!", warning};
    case ActionEvent::restored: return {L"Restored " + revision + L" Revision for " + file + L".", info};
    case ActionEvent::restoreFailed: return {L"Restore of " + revision + L" Revision for " + file + L" Failed!", critical};
    case ActionEvent::reloadFailed: return {L"Restored " + revision + L" Revision for " + file + L"; File Reload Failed!", critical};
    case ActionEvent::deleted: return {file + L" Revision " + revision + L" Deleted.", info};
    case ActionEvent::deleteFailed: return {L"Deletion of " + file + L" Revision " + revision + L" Failed!", warning};
    case ActionEvent::commentUpdated: return {file + L" Revision " + revision + L" Comment Updated.", info};
    case ActionEvent::commentFailed: return {file + L" Revision " + revision + L" Comment Update Failed!", warning};
    case ActionEvent::settingsSaved: return {L"Settings Saved.", info};
    case ActionEvent::settingsFailed: return {L"Settings Save Failed!", critical};
    case ActionEvent::fileSaved: return {file + L" Saved.", info};
    case ActionEvent::savedWithRevision: return {file + L" Saved; Revision Created.", info};
    case ActionEvent::autoSaved: return {file + L" Automatically Saved.", info};
    case ActionEvent::autoSaveFailed: return {L"Automatic Save for " + file + L" Failed!", critical};
    case ActionEvent::updateAvailable: return {check + L": Update Available" + version + L".", info};
    case ActionEvent::upToDate: return {check + L": Up to Date" + version + L".", info};
    case ActionEvent::updateFailed: return {check + L" Failed!", warning};
    }
    return {L"Unknown Action!", warning};
}

void reportAction(ActionEvent event, const ActionContext& context) noexcept
{
    try
    {
        const auto feedback = makeActionFeedback(event, context);
        // Log before posting the transient UI message: coalesced status messages
        // still each have their own log event, including background/bulk saves.
        try
        {
            std::wstring detail;
            const auto append = [&](const std::wstring& label, const std::wstring& value) {
                if (value.empty()) return;
                if (!detail.empty()) detail += L" | ";
                detail += label + singleLine(value);
            };
            append(L"File: ", context.file.wstring());
            append(L"Revision: ", context.revision);
            append(L"Version: ", context.version);
            append(L"Detail: ", context.detail);
            pluginLogger().write(feedback.level, feedback.message, detail);
        }
        catch (...) { /* Feedback must survive an inaccessible log or allocation failure. */ }
        actionStatus().show(feedback.message);
    }
    catch (...) { /* Do not interrupt the underlying action for feedback failure. */ }
}
}
