#pragma once
#include <windows.h>
#include <utility>
#include <vector>

namespace npphistory
{
// Keep unconfigured commands out of the native toolbar rather than leaving
// hidden trailing buttons that other toolbar layout extensions measure as 0px.
int syncToolbarCommands(HWND toolbar, const std::vector<std::pair<int, bool>>& commands);
}
