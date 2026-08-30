#pragma once

#include "UpdateChecker.h"

#include <filesystem>
#include <string>

namespace npphistory
{
bool downloadVerifiedUpdatePackage(const ReleaseInfo& release,
    const std::filesystem::path& dllDestination,
    const std::filesystem::path& updaterDestination, std::wstring& detail);
std::wstring quoteCommandLineArgument(const std::wstring& value);
}
