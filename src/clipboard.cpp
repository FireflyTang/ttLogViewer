#include "clipboard.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <string>
#  include <vector>

// ── Windows clipboard implementation ─────────────────────────────────────────

bool clipboardCopy(const std::string& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    // Convert UTF-8 to UTF-16 for CF_UNICODETEXT
    const int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                         text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (wlen <= 0) { CloseClipboard(); return false; }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
                               static_cast<SIZE_T>(wlen + 1) * sizeof(wchar_t));
    if (!hMem) { CloseClipboard(); return false; }

    auto* dst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!dst) { GlobalFree(hMem); CloseClipboard(); return false; }
    MultiByteToWideChar(CP_UTF8, 0,
                        text.c_str(), static_cast<int>(text.size()),
                        dst, wlen);
    dst[wlen] = L'\0';
    GlobalUnlock(hMem);

    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

ClipboardResult clipboardPaste(std::string& outLine) {
    outLine.clear();
    if (!OpenClipboard(nullptr)) return ClipboardResult::Error;

    // Check if text format is available
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
        !IsClipboardFormatAvailable(CF_TEXT)) {
        CloseClipboard();
        // If clipboard has *any* data, it's non-text (image, etc.)
        return (CountClipboardFormats() > 0) ? ClipboardResult::NotText
                                             : ClipboardResult::Empty;
    }

    // Prefer CF_UNICODETEXT for proper UTF-8 round-trip
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return ClipboardResult::Error; }

    const auto* wstr = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!wstr) { CloseClipboard(); return ClipboardResult::Error; }

    // Convert UTF-16 to UTF-8
    const int wstrLen = static_cast<int>(wcslen(wstr));
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen,
                                            nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen,
                        text.data(), utf8Len, nullptr, nullptr);
    GlobalUnlock(hData);
    CloseClipboard();

    if (text.empty()) return ClipboardResult::Empty;

    // Check for multi-line (contains \n or \r)
    if (text.find('\n') != std::string::npos ||
        text.find('\r') != std::string::npos) {
        return ClipboardResult::MultiLine;
    }

    outLine = std::move(text);
    return ClipboardResult::Ok;
}

#else
// ── Stub for non-Windows platforms ───────────────────────────────────────────

bool clipboardCopy(const std::string&) { return false; }

ClipboardResult clipboardPaste(std::string&) { return ClipboardResult::Error; }

#endif
