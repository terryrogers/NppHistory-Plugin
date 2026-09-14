#include "TemporaryStatusBar.h"
#include <cwchar>
#include <utility>

namespace npphistory
{
TemporaryStatusBar& actionStatus()
{
    static TemporaryStatusBar instance;
    return instance;
}

void TemporaryStatusBar::initialize(HWND owner) noexcept
{
    shutdown();
    _owner = owner;
}

bool TemporaryStatusBar::attach()
{
    if (!_owner || GetWindowThreadProcessId(_owner, nullptr) != GetCurrentThreadId()) return false;
    if (_bar && IsWindow(_bar)) return true;
    _bar = nullptr;
    EnumChildWindows(_owner, [](HWND child, LPARAM value) -> BOOL {
        auto& self = *reinterpret_cast<TemporaryStatusBar*>(value);
        wchar_t name[64]{};
        GetClassNameW(child, name, 64);
        if (GetParent(child) == self._owner && wcscmp(name, STATUSCLASSNAMEW) == 0)
        {
            self._bar = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    if (!_bar) return false;
    if (!SetWindowSubclass(_bar, subclass, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
    {
        _bar = nullptr;
        return false;
    }
    return true;
}

bool TemporaryStatusBar::read(std::wstring& text, UINT& flags) const
{
    if (!_bar || !IsWindow(_bar) || SendMessageW(_bar, SB_GETPARTS, 0, 0) < 1) return false;
    const LRESULT info = SendMessageW(_bar, SB_GETTEXTLENGTHW, 0, 0);
    flags = HIWORD(info);
    if (flags & SBT_OWNERDRAW) return false; // Never interpret another owner's item data as text.
    const unsigned length = LOWORD(info);
    text.assign(length + 1, L'\0');
    SendMessageW(_bar, SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(text.data()));
    text.resize(length);
    return true;
}

void TemporaryStatusBar::show(std::wstring_view text, UINT durationMs) noexcept
{
    try
    {
        if (text.empty() || !attach()) return;
        _pending = L"NppHistory: ";
        _pending.append(text);
        _duration = durationMs ? durationMs : 5000;
        // Wait until the current action/host notification has finished updating its UI.
        // Multiple queued actions coalesce into the latest message, not a backlog.
        if (!PostMessageW(_bar, showMessage, reinterpret_cast<WPARAM>(this), 0)) _pending.clear();
    }
    catch (...) { _pending.clear(); } // Feedback must never interrupt the actual action.
}

void TemporaryStatusBar::displayPending()
{
    if (_pending.empty()) return;
    if (SendMessageW(_bar, SB_ISSIMPLE, 0, 0)) { _pending.clear(); return; }
    std::wstring current;
    UINT flags = 0;
    if (!read(current, flags)) { _pending.clear(); return; }
    if (!_active) { _normal = current; _flags = flags; }
    std::wstring next = std::move(_pending);
    _pending.clear();
    _expires = GetTickCount64() + _duration;
    if (!SetTimer(_bar, timerId, _duration, nullptr)) { release(true); return; }
    _writing = true;
    const bool written = SendMessageW(_bar, SB_SETTEXTW, _flags,
        reinterpret_cast<LPARAM>(next.c_str())) != FALSE;
    _writing = false;
    if (written) { _message = std::move(next); _active = true; }
    else release(true);
}

void TemporaryStatusBar::release(bool restore)
{
    if (!_bar) { _active = false; return; }
    KillTimer(_bar, timerId);
    if (restore && _active)
    {
        std::wstring current;
        UINT flags = 0;
        // Only restore text that we still own. Another plugin or the host may have
        // changed it through a path not observed by our subclass.
        if (read(current, flags) && current == _message && flags == _flags)
        {
            _writing = true;
            SendMessageW(_bar, SB_SETTEXTW, _flags, reinterpret_cast<LPARAM>(_normal.c_str()));
            _writing = false;
        }
    }
    _active = false;
}

void TemporaryStatusBar::clear() noexcept
{
    _pending.clear();
    try { release(true); }
    catch (...) { _active = _writing = false; if (_bar) KillTimer(_bar, timerId); }
}

void TemporaryStatusBar::shutdown() noexcept
{
    clear();
    if (_bar && IsWindow(_bar)) RemoveWindowSubclass(_bar, subclass, reinterpret_cast<UINT_PTR>(this));
    _bar = _owner = nullptr;
}

LRESULT CALLBACK TemporaryStatusBar::subclass(HWND bar, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    auto& self = *reinterpret_cast<TemporaryStatusBar*>(data);
    try
    {
        if (message == showMessage && wParam == reinterpret_cast<WPARAM>(&self))
        {
            self.displayPending();
            return 0;
        }
        if (message == WM_TIMER && wParam == timerId)
        {
            if (self._active && GetTickCount64() >= self._expires) self.release(true);
            return 0;
        }
        if ((message == SB_SETTEXTW || message == SB_SETTEXTA)
            && (wParam & 0xFF) == 0 && !self._writing) self.release(false);
        if (message == SB_SETPARTS || (message == SB_SIMPLE && wParam)) self.clear();
        if (message == WM_NCDESTROY)
        {
            KillTimer(bar, timerId);
            self._bar = nullptr;
            self._active = self._writing = false;
            self._pending.clear();
            RemoveWindowSubclass(bar, subclass, id);
        }
    }
    catch (...)
    {
        self._active = self._writing = false;
        self._pending.clear();
        KillTimer(bar, timerId);
    }
    return DefSubclassProc(bar, message, wParam, lParam);
}
}
