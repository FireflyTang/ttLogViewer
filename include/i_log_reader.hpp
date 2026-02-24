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

// Abstract interface for log file reading.
// All methods must be called from the UI thread unless otherwise noted.
class ILogReader {
public:
    virtual ~ILogReader() = default;

    virtual bool open(std::string_view path) = 0;
    virtual void close() = 0;

    // 1-based line access. Returned view is valid until the next remap.
    virtual std::string_view              getLine(size_t lineNo)           const = 0;
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
