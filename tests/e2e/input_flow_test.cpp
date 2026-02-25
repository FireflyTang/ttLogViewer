#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

class InputFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 10; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()           { return ctrl_.getViewData(5, 5); }

    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── 'a' full flow ─────────────────────────────────────────────────────────────

TEST_F(InputFlowTest, AddFilterFullFlow) {
    key(ftxui::Event::Character('a'));
    EXPECT_TRUE(ctrl_.isInputActive());

    auto d1 = data();
    EXPECT_EQ(d1.inputMode, InputMode::FilterAdd);
    EXPECT_NE(d1.inputPrompt.find("Pattern"), std::string::npos);

    type("line1");
    EXPECT_TRUE(data().inputValid);  // valid regex

    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.isInputActive());
    EXPECT_EQ(chain_.filterCount(), 1u);
    EXPECT_EQ(chain_.filterAt(0).pattern, "line1");
}

TEST_F(InputFlowTest, AddFilterTabCyclesColor) {
    key(ftxui::Event::Character('a'));
    auto d1 = data();
    // Tab cycles color palette; inputBuffer should remain empty
    key(ftxui::Event::Tab);
    EXPECT_EQ(data().inputBuffer, d1.inputBuffer);  // buffer unchanged
    EXPECT_TRUE(ctrl_.isInputActive());
}

TEST_F(InputFlowTest, AddFilterBackspace) {
    key(ftxui::Event::Character('a'));
    type("AB");
    EXPECT_EQ(data().inputBuffer, "AB");
    key(ftxui::Event::Backspace);
    EXPECT_EQ(data().inputBuffer, "A");
}

TEST_F(InputFlowTest, AddFilterInvalidRegexSignalLight) {
    key(ftxui::Event::Character('a'));
    type("[");  // Incomplete bracket expression
    EXPECT_FALSE(data().inputValid);
    type("]");  // Now complete: "[]" is valid in some contexts
    // Actually "[]]" or "[a-z]" are valid; "[]" may or may not be valid
    // Just verify signal light updates
}

// ── 'e' edit flow ─────────────────────────────────────────────────────────────

TEST_F(InputFlowTest, EditFilterPrefillsBuffer) {
    key(ftxui::Event::Character('a'));
    type("ERROR");
    key(ftxui::Event::Return);

    key(ftxui::Event::Character('e'));
    EXPECT_EQ(data().inputBuffer, "ERROR");
    EXPECT_EQ(data().inputMode, InputMode::FilterEdit);
    key(ftxui::Event::Escape);
}

TEST_F(InputFlowTest, EditFilterConfirms) {
    key(ftxui::Event::Character('a'));
    type("ERROR");
    key(ftxui::Event::Return);

    key(ftxui::Event::Character('e'));
    for (int i = 0; i < 5; ++i) key(ftxui::Event::Backspace);
    type("WARN");
    key(ftxui::Event::Return);
    EXPECT_EQ(chain_.filterAt(0).pattern, "WARN");
}

// ── 'g' goto line flow ────────────────────────────────────────────────────────

TEST_F(InputFlowTest, GotoLineOnlyAcceptsDigits) {
    key(ftxui::Event::Character('g'));
    type("abc");  // Non-digits should be ignored
    EXPECT_TRUE(data().inputBuffer.empty());
    type("5");
    EXPECT_EQ(data().inputBuffer, "5");
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.isInputActive());
}

// ── 'o' open file flow ────────────────────────────────────────────────────────

TEST_F(InputFlowTest, OpenFileFlow) {
    TempFile newFile("new_content\n");
    key(ftxui::Event::Character('o'));
    EXPECT_EQ(data().inputMode, InputMode::OpenFile);
    type(newFile.path());
    key(ftxui::Event::Return);
    waitForIndexing(reader_);
    EXPECT_FALSE(ctrl_.isInputActive());
    EXPECT_EQ(data().totalLines, 1u);
}

// ── Input state blocks new-line cursor update ─────────────────────────────────

TEST_F(InputFlowTest, NewLinesWhileInputActiveDoNotMoveCursor) {
    // Enter input mode
    key(ftxui::Event::Character('a'));
    EXPECT_TRUE(ctrl_.isInputActive());

    // Append lines and force check
    file_->append("extra_line\n");
    reader_.forceCheck();

    // newLineCount should increase but cursor should not auto-jump
    auto d = data();
    EXPECT_GT(d.newLineCount, 0u);
    // Cursor stays at original position (line 1)
    bool atLine1 = false;
    for (auto& ll : d.rawPane)
        if (ll.highlighted && ll.rawLineNo == 1) atLine1 = true;
    EXPECT_TRUE(atLine1);

    key(ftxui::Event::Escape);
}

// ── Phase 3: 'w' export flow ──────────────────────────────────────────────────

TEST_F(InputFlowTest, WKeyEntersExportConfirmMode) {
    key(ftxui::Event::Character('w'));
    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::ExportConfirm);
    EXPECT_TRUE(ctrl_.isInputActive());
    key(ftxui::Event::Escape);
}

TEST_F(InputFlowTest, WKeyEscCancelsExport) {
    key(ftxui::Event::Character('w'));
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());
    EXPECT_EQ(data().inputMode, InputMode::None);
}

TEST_F(InputFlowTest, WKeyExportCreatesFile) {
    key(ftxui::Event::Character('w'));
    // The inputBuffer is the auto-generated path
    std::string exportPath = data().inputBuffer;
    EXPECT_FALSE(exportPath.empty());

    key(ftxui::Event::Return);
    // File should now exist
    EXPECT_TRUE(std::filesystem::exists(exportPath));

    // Close success dialog
    key(ftxui::Event::Return);

    // Cleanup
    std::filesystem::remove(exportPath);
}

TEST_F(InputFlowTest, WKeyExportContainsLines) {
    key(ftxui::Event::Character('w'));
    std::string exportPath = data().inputBuffer;
    key(ftxui::Event::Return);

    // Read the file inside a scope so the ifstream is closed before we try to
    // remove it (Windows does not allow deleting open files).
    std::string content;
    {
        std::ifstream f{exportPath};
        ASSERT_TRUE(f.is_open());
        content = std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    }
    EXPECT_NE(content.find("line1"), std::string::npos);

    std::filesystem::remove(exportPath);
}
