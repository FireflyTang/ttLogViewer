#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "i_log_reader.hpp"

// Reads a log file via mmap and builds a line-offset index.
// All public methods must be called from the UI thread,
// EXCEPT getLine() / getLines() which are safe from any thread
// as long as the caller holds a mmapAnchor() shared_ptr.
class LogReader : public ILogReader {
public:
    LogReader();
    ~LogReader() override;

    LogReader(const LogReader&)            = delete;
    LogReader& operator=(const LogReader&) = delete;

    // Inject post-to-UI function. Must be called before open() when threading
    // is needed. Default: synchronous (calls immediately).
    void setPostFn(PostFn fn);

    // Open file and build line index synchronously.
    // Starts FileWatcher thread if current mode is Realtime.
    // Returns false if the file cannot be read.
    bool open(std::string_view path)  override;
    void close()                      override;

    // Zero-copy line access. Safe from any thread as long as caller holds
    // a mmapAnchor() shared_ptr to keep the mapping alive.
    // lineNo is 1-based. Returns empty view for out-of-range lineNo.
    std::string_view              getLine(size_t lineNo)           const override;
    std::vector<std::string_view> getLines(size_t from, size_t to) const override;

    size_t   lineCount()  const override;
    bool     isIndexing() const override;  // Phase 1: always false

    void     setMode(FileMode mode) override;
    FileMode mode()       const     override;

    // Synchronously check for file changes and invoke callbacks if any.
    void forceCheck() override;

    void onNewLines(NewLinesCallback  cb) override;
    void onFileReset(FileResetCallback cb) override;

    std::string_view filePath() const override;

    // Returns shared ownership of the current mmap region.
    // Hold this to keep string_views from getLine() alive across threads.
    std::shared_ptr<void> mmapAnchor() const override;

private:
    // Platform mmap wrapper – defined in log_reader.cpp only.
    struct MmapRegion;
    std::shared_ptr<MmapRegion> mmap_;

    std::string           path_;
    FileMode              mode_     = FileMode::Static;
    std::vector<uint64_t> lineOffsets_;   // lineOffsets_[i] = byte offset of line (i+1)
    bool                  isOpen_   = false;
    size_t                processedSize_ = 0;  // Bytes processed by doCheck

    NewLinesCallback  newLinesCb_;
    FileResetCallback fileResetCb_;
    PostFn            postFn_;

    // FileWatcher thread (Realtime mode only)
    std::thread       watcherThread_;
    std::atomic<bool> stopWatcher_{false};
    std::mutex        checkMutex_;

    void buildIndex();
    void startWatcher();
    void stopWatcher();
    void doCheck();        // Called in UI thread; checks file for changes
    void watcherLoop();    // Runs in watcherThread_
};
