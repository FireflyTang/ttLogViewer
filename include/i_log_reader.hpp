#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class FileMode { Static, Realtime };

// PostFn: post a closure to be executed in the UI thread.
// Default (synchronous): calls the closure immediately in the calling thread.
// In production: wraps ftxui::ScreenInteractive::Post(closure).
using PostFn = std::function<void(std::function<void()>)>;

using NewLinesCallback  = std::function<void(size_t firstLine, size_t lastLine)>;
using FileResetCallback = std::function<void()>;

// Abstract interface for log file reading with memory-mapped file support.
// Thread safety: All methods (except getLine/getLines) must be called from
// the UI thread only. getLine/getLines can be called from any thread if the
// caller holds a shared_ptr from mmapAnchor() to prevent the memory map from
// being deallocated during access.
class ILogReader {
public:
    virtual ~ILogReader() = default;

    virtual bool open(std::string_view path) = 0;
    virtual void close() = 0;

    // Returns the content of the line at the given 1-based line number.
    // Line numbers start from 1; lineNo=0 or lineNo > lineCount() returns empty view.
    // The returned string_view is valid until the next file remap (file growth/change).
    // Thread safety: Safe to call from any thread if caller holds mmapAnchor().
    virtual std::string_view              getLine(size_t lineNo)           const = 0;

    // Returns multiple lines in range [from, to] inclusive (1-based).
    // Same lifetime and thread safety guarantees as getLine().
    virtual std::vector<std::string_view> getLines(size_t from, size_t to) const = 0;

    virtual size_t   lineCount()  const = 0;
    virtual bool     isIndexing() const = 0;

    virtual void     setMode(FileMode mode) = 0;
    virtual FileMode mode()       const     = 0;

    virtual void     forceCheck() = 0;

    virtual void onNewLines(NewLinesCallback  cb) = 0;
    virtual void onFileReset(FileResetCallback cb) = 0;

    virtual std::string_view filePath() const = 0;

    // Returns a shared ownership handle that keeps the underlying memory map
    // alive for as long as the handle is held. Used by background threads
    // (reprocess, search) that hold string_views into mmap data.
    virtual std::shared_ptr<void> mmapAnchor() const = 0;
};
