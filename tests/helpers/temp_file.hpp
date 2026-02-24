#pragma once
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

// RAII temporary file for tests.
// Created in the system's temp directory; automatically deleted on destruction.
class TempFile {
public:
    explicit TempFile(std::string_view content = "") {
        namespace fs = std::filesystem;
        path_ = (fs::temp_directory_path() / "ttlv_test_XXXXXX.log").string();

        // Generate a unique name via a simple counter
        static std::atomic<int> counter{0};
        path_ = (fs::temp_directory_path()
                 / ("ttlv_test_" + std::to_string(counter++) + ".log")).string();

        std::ofstream f(path_, std::ios::binary);
        if (!f) throw std::runtime_error("TempFile: cannot create " + path_);
        if (!content.empty()) f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);  // Ignore errors (file may still be held open on Windows)
    }

    // Write additional bytes at the end of the file.
    void append(std::string_view content) {
        std::ofstream f(path_, std::ios::binary | std::ios::app);
        if (content.empty()) return;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    // Truncate the file and write new content (simulates log rotation).
    void truncateAndWrite(std::string_view content) {
        std::ofstream f(path_, std::ios::binary | std::ios::trunc);
        if (!content.empty()) f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    const std::string& path() const { return path_; }

    // Non-copyable
    TempFile(const TempFile&)            = delete;
    TempFile& operator=(const TempFile&) = delete;

private:
    std::string path_;
};
