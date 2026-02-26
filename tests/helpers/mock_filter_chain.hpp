#pragma once
#include <gmock/gmock.h>
#include "i_filter_chain.hpp"

class MockFilterChain : public IFilterChain {
public:
    MOCK_METHOD(bool, append, (FilterDef def),               (override));
    MOCK_METHOD(void, remove, (size_t index),                (override));
    MOCK_METHOD(bool, edit,   (size_t index, FilterDef def), (override));
    MOCK_METHOD(void, moveUp,   (size_t index), (override));
    MOCK_METHOD(void, moveDown, (size_t index), (override));

    MOCK_METHOD(size_t,           filterCount, (),            (const, override));
    MOCK_METHOD(const FilterDef&, filterAt,    (size_t index),(const, override));

    MOCK_METHOD(void,   toggleUseRegex,      (size_t idx),           (override));
    MOCK_METHOD(size_t, filteredLineCountAt, (size_t idx),           (const, override));

    MOCK_METHOD(size_t,              filteredLineCount, (),                         (const, override));
    MOCK_METHOD(size_t,              filteredLineAt,    (size_t filteredIndex),      (const, override));
    MOCK_METHOD(std::vector<size_t>, filteredLines,     (size_t from, size_t count),(const, override));

    MOCK_METHOD(std::vector<ColorSpan>, computeColors,
                (size_t rawLineNo, std::string_view content), (const, override));

    MOCK_METHOD(void, processNewLines, (size_t firstLine, size_t lastLine), (override));
    MOCK_METHOD(void, reprocess,
                (size_t fromFilter, ProgressCallback onProgress, DoneCallback onDone),
                (override));
    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(void, save,  (std::string_view path), (const, override));
    MOCK_METHOD(bool, load,  (std::string_view path), (override));
};
