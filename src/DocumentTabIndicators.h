#pragma once

#include <windows.h>
#include <unordered_map>

namespace npphistory
{
// Keys are the native TCIF_PARAM buffer identities, not tab positions.
void updateDocumentTabDecorations(HWND tabs, const std::unordered_map<LPARAM, unsigned>& masks,
    HINSTANCE resources);
void removeDocumentTabDecorations(HWND tabs);

struct DocumentTabDecorationMetrics
{
    // Distances follow the text axis: left-to-right horizontally, bottom-to-top vertically.
    bool vertical = false;
    int originalWidth = 0;
    int reservedWidth = 0;
    int textRight = 0;
    int iconsLeft = 0;
    int iconsRight = 0;
    // Start of the original native trailing area (including its own button gap).
    int nativeTrailingStart = 0;
    int nativeTrailingSpace = 0;
    int spacerUnit = 0;
    unsigned mask = 0;
};
bool documentTabDecorationMetrics(HWND tabs, int index, DocumentTabDecorationMetrics& metrics);
}
