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

// Encoding detected from BOM at the start of the file.
// UTF-8 without BOM is treated as Utf8 (the common case).
// UTF-8 with BOM is also normalized to Utf8 (BOM stripped).
enum class FileEncoding { Utf8, Utf16Le, Utf16Be };

// Reads a log file via mmap and builds a line-offset index.
// All public methods must be called from the UI thread,
// EXCEPT getLine() / getLines() which are safe from any thread
// as long as the caller holds a mmapAnchor() shared_ptr.
//
// Thread model:
//   IndexThread  – background; populates lineOffsets_[], increments lineCount_ (release).
//   FileWatcher  – background; posts doCheck() calls back to the UI thread via postFn_.
//   UI thread    – reads lineCount_ (acquire) then lineOffsets_[0..lineCount_-1].
//
// Invariant: lineOffsets_ is append-only and never reallocated after IndexThread starts.
// IndexThread reserves capacity upfront (two-pass: count then fill).
//
// Encoding support:
//   BOM is detected synchronously in open().  UTF-16LE/BE files are decoded to
//   a UTF-8 string (decoded_) once; subsequent indexing and line reads operate
//   on that buffer.  UTF-8 BOM files have the 3-byte BOM stripped into decoded_.
//   Realtime mode for non-UTF-8 files: file growth triggers a full re-open
//   (fileResetCb_) because decoded offsets can't be incrementally mapped to
//   raw mmap offsets cheaply.
class LogReader : public ILogReader {
public:
    LogReader();
    ~LogReader() override;

    LogReader(const LogReader&)            = delete;
    LogReader& operator=(const LogReader&) = delete;

    // Inject post-to-UI function. Must be called before open() when threading
    // is needed. Default: synchronous (calls immediately).
    void setPostFn(PostFn fn);

    // Open file and start background index thread.
    // Starts FileWatcher thread too if current mode is Realtime.
    // Returns false if the file cannot be read.
    bool open(std::string_view path)  override;
    void close()                      override;

    // Zero-copy line access. Safe from any thread as long as caller holds
    // a mmapAnchor() shared_ptr to keep the mapping alive.
    // lineNo is 1-based. Returns empty view for out-of-range lineNo.
    std::string_view              getLine(size_t lineNo)           const override;
    std::vector<std::string_view> getLines(size_t from, size_t to) const override;

    // lineCount() returns the number of lines indexed so far (grows while indexing).
    size_t   lineCount()  const override;
    // isIndexing() returns true while the background index thread is running.
    bool     isIndexing() const override;

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

    // Returns the encoding detected from the file BOM.
    FileEncoding detectedEncoding() const { return encoding_; }

private:
    // Platform mmap wrapper – defined in log_reader.cpp only.
    struct MmapRegion;
    std::shared_ptr<MmapRegion> mmap_;

    std::string           path_;
    FileMode              mode_     = FileMode::Static;

    // Encoding support.
    // decoded_ is non-null for UTF-16LE/BE files and UTF-8 BOM files.
    // It holds the normalized UTF-8 content; indexing and getLine() use it
    // instead of mmap_ when set.
    FileEncoding                 encoding_ = FileEncoding::Utf8;
    std::shared_ptr<std::string> decoded_;

    // Returns the base pointer and byte size of the content to index/read.
    // If decoded_ is set, returns decoded_ data; otherwise mmap_ data.
    const char* contentData() const;
    size_t      contentSize() const;

    // lineOffsets_[i] = byte offset of line (i+1).
    // Append-only; capacity is reserved before IndexThread starts, so no
    // reallocation occurs while IndexThread is running.
    std::vector<uint64_t> lineOffsets_;

    // Number of lines fully indexed so far.  Written by IndexThread with
    // memory_order_release; read by UI thread with memory_order_acquire.
    std::atomic<size_t>   lineCount_{0};

    bool   isOpen_        = false;
    size_t processedSize_ = 0;  // Bytes processed by doCheck

    NewLinesCallback  newLinesCb_;
    FileResetCallback fileResetCb_;
    PostFn            postFn_;

    // ── IndexThread ───────────────────────────────────────────────────────────
    std::thread       indexThread_;
    std::atomic<bool> stopIndex_{false};
    std::atomic<bool> isIndexing_{false};

    void startIndexThread();
    void stopIndexThread();
    void indexLoop();   // Runs in indexThread_

    // ── FileWatcher thread (Realtime mode only) ────────────────────────────
    std::thread       watcherThread_;
    std::atomic<bool> stopWatcher_{false};
    std::mutex        checkMutex_;

    void startWatcher();
    void stopWatcher();
    void doCheck();        // Called in UI thread; checks file for changes
    void watcherLoop();    // Runs in watcherThread_
};
