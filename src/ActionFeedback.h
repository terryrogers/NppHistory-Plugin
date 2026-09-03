#pragma once
#include "Settings.h"
#include <filesystem>
#include <string>

namespace npphistory
{
enum class ActionEvent
{
    captured, captureFailed, refreshed, comparisonOpened, comparisonFailed,
    restored, restoreFailed, reloadFailed, deleted, deleteFailed,
    commentUpdated, commentFailed, settingsSaved, settingsFailed,
    fileSaved, savedWithRevision, autoSaved, autoSaveFailed,
    updateAvailable, upToDate, updateFailed
};

struct ActionContext
{
    std::filesystem::path file;
    std::wstring revision;
    std::wstring version;
    bool automatic = false;
    std::wstring detail;
};

struct ActionFeedback
{
    std::wstring message;
    LogLevel level;
};

ActionFeedback makeActionFeedback(ActionEvent event, const ActionContext& context = {});
// One call pairs status feedback with the same human-readable log event. Logging
// respects the configured switch/threshold; a logging failure cannot suppress UI feedback.
void reportAction(ActionEvent event, const ActionContext& context = {}) noexcept;
}
