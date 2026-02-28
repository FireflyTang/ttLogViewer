#pragma once
#include <string>

// Result of a paste operation.
enum class ClipboardResult {
    Ok,         // Single-line text successfully read
    NotText,    // Clipboard contains non-text data (image, etc.)
    MultiLine,  // Clipboard text contains multiple lines
    Empty,      // Clipboard is empty
    Error,      // Platform error or unsupported platform
};

// Copy UTF-8 text to the system clipboard.  Returns true on success.
bool clipboardCopy(const std::string& text);

// Read a single line of text from the system clipboard.
// On success (Ok), `outLine` contains the trimmed single-line text.
// On MultiLine, `outLine` is empty (caller should show error).
ClipboardResult clipboardPaste(std::string& outLine);
