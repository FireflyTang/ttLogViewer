#include "log_reader.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

// ── Platform-specific file mapping ────────────────────────────────────────────
//
// Windows (MinGW64 / MSVC): CreateFileMapping / MapViewOfFile.
// POSIX (Linux, macOS): mmap.

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

// ── MmapRegion ─────────────────────────────────────────────────────────────────

struct LogReader::MmapRegion {
    const char* ptr  = nullptr;
    size_t      size = 0;

#ifdef _WIN32
    HANDLE hFile    = INVALID_HANDLE_VALUE;
    HANDLE hMapping = nullptr;
#else
    int fd = -1;
#endif

    MmapRegion() = default;

    MmapRegion(const MmapRegion&)            = delete;
    MmapRegion& operator=(const MmapRegion&) = delete;

    bool open(const std::string& path) {
#ifdef _WIN32
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER li{};
        if (!GetFileSizeEx(hFile, &li)) {
            CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        size = static_cast<size_t>(li.QuadPart);

        if (size == 0) return true;  // Empty file: valid, but cannot map 0 bytes

        hMapping = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMapping) {
            CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; size = 0;
            return false;
        }

        ptr = static_cast<const char*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0));
        if (!ptr) {
            CloseHandle(hMapping); hMapping = nullptr;
            CloseHandle(hFile);    hFile    = INVALID_HANDLE_VALUE;
            size = 0;
            return false;
        }
        return true;
#else
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st{};
        if (::fstat(fd, &st) < 0) { ::close(fd); fd = -1; return false; }
        size = static_cast<size_t>(st.st_size);

        if (size == 0) return true;

        void* p = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) { ::close(fd); fd = -1; size = 0; return false; }
        ptr = static_cast<const char*>(p);
        return true;
#endif
    }

    void close() {
#ifdef _WIN32
        if (ptr)     { UnmapViewOfFile(ptr); ptr = nullptr; }
        if (hMapping){ CloseHandle(hMapping); hMapping = nullptr; }
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE;
        }
#else
        if (ptr) { ::munmap(const_cast<char*>(ptr), size); ptr = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
        size = 0;
    }

    ~MmapRegion() { close(); }

    const char* data() const { return ptr; }
};

// ── Platform helpers ───────────────────────────────────────────────────────────

static size_t getFileSize(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attrs))
        return 0;
    LARGE_INTEGER li;
    li.LowPart  = attrs.nFileSizeLow;
    li.HighPart = static_cast<LONG>(attrs.nFileSizeHigh);
    return static_cast<size_t>(li.QuadPart);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
#endif
}

// ── LogReader ──────────────────────────────────────────────────────────────────

LogReader::LogReader()
    : postFn_([](std::function<void()> fn) { fn(); })  // synchronous default
{}

LogReader::~LogReader() {
    stopWatcher();
}

void LogReader::setPostFn(PostFn fn) {
    postFn_ = fn ? std::move(fn)
                 : [](std::function<void()> f) { f(); };
}

bool LogReader::open(std::string_view path) {
    stopWatcher();
    close();

    path_ = std::string(path);

    mmap_ = std::make_shared<MmapRegion>();
    if (!mmap_->open(path_)) {
        mmap_.reset();
        return false;
    }

    buildIndex();
    processedSize_ = mmap_ ? mmap_->size : 0;
    isOpen_ = true;

    if (mode_ == FileMode::Realtime)
        startWatcher();

    return true;
}

void LogReader::close() {
    stopWatcher();
    isOpen_ = false;
    lineOffsets_.clear();
    mmap_.reset();
    path_.clear();
    processedSize_ = 0;
}

// Scan the mmap region and populate lineOffsets_.
// lineOffsets_[i] = byte offset of the first character of line (i+1).
void LogReader::buildIndex() {
    lineOffsets_.clear();

    const char* data = mmap_ ? mmap_->data() : nullptr;
    const size_t sz  = mmap_ ? mmap_->size   : 0;

    if (!data || sz == 0) return;

    lineOffsets_.push_back(0);  // Line 1 starts at byte 0

    for (size_t i = 0; i < sz; ++i) {
        if (data[i] == '\n' && i + 1 < sz) {
            lineOffsets_.push_back(i + 1);
        }
    }
}

std::string_view LogReader::getLine(size_t lineNo) const {
    if (!isOpen_ || lineNo == 0 || lineNo > lineOffsets_.size())
        return {};

    const char* data = mmap_ ? mmap_->data() : nullptr;
    if (!data) return {};

    const size_t start = lineOffsets_[lineNo - 1];
    size_t end;

    if (lineNo < lineOffsets_.size()) {
        // The '\n' that terminates this line is at (lineOffsets_[lineNo] - 1)
        end = lineOffsets_[lineNo] - 1;
    } else {
        end = mmap_->size;
    }

    // Strip trailing '\n' (needed for the last line which uses end=mmap_->size)
    if (end > start && data[end - 1] == '\n') --end;
    // Strip trailing '\r' for CRLF files
    if (end > start && data[end - 1] == '\r') --end;

    if (end <= start) return {};
    return {data + start, end - start};
}

std::vector<std::string_view> LogReader::getLines(size_t from, size_t to) const {
    std::vector<std::string_view> result;
    if (from > to) return result;

    to = std::min(to, lineOffsets_.size());
    result.reserve(to - from + 1);

    for (size_t i = from; i <= to; ++i)
        result.push_back(getLine(i));

    return result;
}

size_t   LogReader::lineCount()  const { return lineOffsets_.size(); }
bool     LogReader::isIndexing() const { return false; }

void LogReader::setMode(FileMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    if (isOpen_) {
        if (mode_ == FileMode::Realtime)
            startWatcher();
        else
            stopWatcher();
    }
}

FileMode LogReader::mode() const { return mode_; }

std::string_view LogReader::filePath() const { return path_; }

std::shared_ptr<void> LogReader::mmapAnchor() const {
    return mmap_;  // implicit upcast shared_ptr<MmapRegion> -> shared_ptr<void>
}

void LogReader::onNewLines(NewLinesCallback cb)  { newLinesCb_  = std::move(cb); }
void LogReader::onFileReset(FileResetCallback cb) { fileResetCb_ = std::move(cb); }

// ── FileWatcher ────────────────────────────────────────────────────────────────

void LogReader::startWatcher() {
    stopWatcher_.store(false);
    watcherThread_ = std::thread([this]() { watcherLoop(); });
}

void LogReader::stopWatcher() {
    stopWatcher_.store(true);
    if (watcherThread_.joinable())
        watcherThread_.join();
}

void LogReader::watcherLoop() {
    using namespace std::chrono_literals;
    // Poll interval: 50 ticks × 10 ms = 500 ms between file-size checks.
    // Fine-grained ticks allow the watcher thread to stop promptly on request.
    static constexpr int  kTickCount    = 50;
    static constexpr auto kTickInterval = 10ms;

    while (!stopWatcher_.load()) {
        for (int i = 0; i < kTickCount && !stopWatcher_.load(); ++i)
            std::this_thread::sleep_for(kTickInterval);
        if (stopWatcher_.load()) break;

        // Post doCheck to UI thread
        postFn_([this]() {
            if (isOpen_) doCheck();
        });
    }
}

// ── forceCheck / doCheck ──────────────────────────────────────────────────────

void LogReader::forceCheck() {
    if (!isOpen_) return;
    doCheck();
}

void LogReader::doCheck() {
    // Always called from UI thread (or synchronous postFn context in tests).
    // No locking needed: forceCheck() is UI-thread-only; FileWatcher posts via
    // postFn_ which serializes execution through the UI event loop.
    const size_t currentSize = getFileSize(path_);

    if (currentSize > processedSize_) {
        // File grew: remap and index new lines
        const size_t firstNewLine = lineOffsets_.size() + 1;

        auto newMmap = std::make_shared<MmapRegion>();
        if (!newMmap->open(path_)) return;

        const char* data = newMmap->data();

        // If the previously-processed content ended with '\n', the very first
        // byte of new content starts a new line.  We must record that offset now
        // (it was NOT recorded during the earlier scan because the condition
        // "i + 1 < sz" would have been false at the trailing newline position).
        if (processedSize_ > 0 && data[processedSize_ - 1] == '\n')
            lineOffsets_.push_back(processedSize_);

        for (size_t i = processedSize_; i < currentSize; ++i) {
            if (data[i] == '\n' && i + 1 < currentSize)
                lineOffsets_.push_back(i + 1);
        }

        mmap_ = std::move(newMmap);
        processedSize_ = currentSize;

        const size_t lastNewLine = lineOffsets_.size();
        if (lastNewLine >= firstNewLine && newLinesCb_)
            newLinesCb_(firstNewLine, lastNewLine);

    } else if (currentSize < processedSize_) {
        // File truncated or replaced
        if (fileResetCb_) fileResetCb_();
    }
    // currentSize == processedSize_: no change
}
