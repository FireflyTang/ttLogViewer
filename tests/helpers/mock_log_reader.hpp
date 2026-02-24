#pragma once
#include <gmock/gmock.h>
#include "i_log_reader.hpp"

class MockLogReader : public ILogReader {
public:
    MOCK_METHOD(bool, open,  (std::string_view path), (override));
    MOCK_METHOD(void, close, (),                       (override));

    MOCK_METHOD(std::string_view,              getLine,  (size_t lineNo),          (const, override));
    MOCK_METHOD(std::vector<std::string_view>, getLines, (size_t from, size_t to), (const, override));

    MOCK_METHOD(size_t,   lineCount,  (), (const, override));
    MOCK_METHOD(bool,     isIndexing, (), (const, override));

    MOCK_METHOD(void,     setMode, (FileMode mode), (override));
    MOCK_METHOD(FileMode, mode,    (),               (const, override));

    MOCK_METHOD(void, forceCheck, (), (override));

    MOCK_METHOD(void, onNewLines,  (NewLinesCallback  cb), (override));
    MOCK_METHOD(void, onFileReset, (FileResetCallback cb), (override));

    MOCK_METHOD(std::string_view,          filePath,   (), (const, override));
    MOCK_METHOD(std::shared_ptr<void>,     mmapAnchor, (), (const, override));
};
