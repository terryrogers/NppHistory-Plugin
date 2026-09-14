#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <string_view>

namespace npphistory
{
// Borrows only the native document-type field. No new parts, resizing or polling
// writes: an external write to that field always takes ownership back immediately.
class TemporaryStatusBar
{
public:
    TemporaryStatusBar() = default;
    TemporaryStatusBar(const TemporaryStatusBar&) = delete;
    TemporaryStatusBar& operator=(const TemporaryStatusBar&) = delete;
    static constexpr UINT_PTR timerId = 0x4E485354;
    static constexpr UINT showMessage = WM_APP + 247;
    void initialize(HWND owner) noexcept;
    void show(std::wstring_view text, UINT durationMs = 5000) noexcept;
    void clear() noexcept;
    void shutdown() noexcept;

private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    bool attach();
    bool read(std::wstring& text, UINT& flags) const;
    void displayPending();
    void release(bool restore);
    HWND _owner = nullptr;
    HWND _bar = nullptr;
    bool _active = false;
    bool _writing = false;
    std::wstring _normal, _message, _pending;
    UINT _flags = 0, _duration = 5000;
    ULONGLONG _expires = 0;
};

TemporaryStatusBar& actionStatus();
}
