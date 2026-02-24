#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class FileMode { Static, Realtime };

using NewLinesCallback  = std::function<void(size_t firstLine, size_t lastLine)>;
using FileResetCallback = std::function<void()>;

// Reads a log file via mmap and builds a line-offset index.
// All public methods must be called from the UI thread only.
class LogReader {
public:
    LogReader();
    ~LogReader();  // Defined in .cpp so MmapRegion can be forward-declared here

    LogReader(const LogReader&)            = delete;
    LogReader& operator=(const LogReader&) = delete;

    // Open file and build line index synchronously (Phase 1).
    // Returns false if the file cannot be read.
    bool open(std::string_view path);

    void close();

    // Zero-copy line access.
    // Returned view is valid until the next forceCheck() or file reset.
    // lineNo is 1-based. Returns empty view for out-of-range lineNo.
    std::string_view              getLine(size_t lineNo) const;
    std::vector<std::string_view> getLines(size_t from, size_t to) const;

    size_t   lineCount()  const;   // Number of indexed lines
    bool     isIndexing() const;   // True while background index is running (Phase 2)

    void     setMode(FileMode mode);
    FileMode mode()       const;

    // Synchronously check for file changes and invoke callbacks if any.
    // Phase 1: stub (no-op). Phase 2: full implementation.
    void forceCheck();

    // Register callbacks (invoked in UI thread via PostEvent in Phase 2).
    void onNewLines(NewLinesCallback cb);
    void onFileReset(FileResetCallback cb);

    std::string_view filePath() const;

private:
    // Platform mmap wrapper – defined in log_reader.cpp only.
    struct MmapRegion;
    std::unique_ptr<MmapRegion> mmap_;

    std::string           path_;
    FileMode              mode_      = FileMode::Realtime;
    std::vector<uint64_t> lineOffsets_;   // lineOffsets_[i] = byte offset of line (i+1)
    bool                  isOpen_    = false;

    NewLinesCallback  newLinesCb_;
    FileResetCallback fileResetCb_;

    void buildIndex();
};
