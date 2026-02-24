#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"

class SearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        content += "apple pie\n";      // line1
        content += "banana split\n";   // line2
        content += "apple cider\n";    // line3
        content += "cherry tart\n";    // line4
        content += "apple juice\n";    // line5
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }

    size_t highlightedLine() {
        for (auto& ll : ctrl_.getViewData(5, 5).rawPane)
            if (ll.highlighted) return ll.rawLineNo;
        return 0;
    }

    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    void search(const std::string& kw) {
        key(ftxui::Event::Character('/'));
        type(kw);
        key(ftxui::Event::Return);
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── Search ────────────────────────────────────────────────────────────────────

TEST_F(SearchTest, SearchFindsFirstMatch) {
    search("apple");
    EXPECT_EQ(highlightedLine(), 1u);
}

TEST_F(SearchTest, NKeyGoesToNextResult) {
    search("apple");
    EXPECT_EQ(highlightedLine(), 1u);  // first result
    key(ftxui::Event::Character('n'));
    EXPECT_EQ(highlightedLine(), 3u);  // second result
    key(ftxui::Event::Character('n'));
    EXPECT_EQ(highlightedLine(), 5u);  // third result
}

TEST_F(SearchTest, NKeyWrapsAround) {
    search("apple");
    key(ftxui::Event::Character('n'));
    key(ftxui::Event::Character('n'));
    // At result 3 (line5); next wraps to result 1 (line1)
    key(ftxui::Event::Character('n'));
    EXPECT_EQ(highlightedLine(), 1u);
}

TEST_F(SearchTest, PKeyGoesToPrevResult) {
    search("apple");
    key(ftxui::Event::Character('n'));  // line3
    key(ftxui::Event::Character('p'));  // back to line1
    EXPECT_EQ(highlightedLine(), 1u);
}

TEST_F(SearchTest, PKeyWrapsAround) {
    search("apple");
    // At result 1 (line1); prev wraps to result 3 (line5)
    key(ftxui::Event::Character('p'));
    EXPECT_EQ(highlightedLine(), 5u);
}

TEST_F(SearchTest, NoResultsNPDoNothing) {
    search("zzz_not_found");
    // n and p should not crash or change cursor
    size_t before = highlightedLine();
    key(ftxui::Event::Character('n'));
    key(ftxui::Event::Character('p'));
    EXPECT_EQ(highlightedLine(), before);
}

TEST_F(SearchTest, SingleResult) {
    search("banana");
    EXPECT_EQ(highlightedLine(), 2u);
    key(ftxui::Event::Character('n'));  // wraps to itself
    EXPECT_EQ(highlightedLine(), 2u);
}

TEST_F(SearchTest, EscClearsSearch) {
    search("apple");
    key(ftxui::Event::Character('/'));
    key(ftxui::Event::Escape);
    // After Esc in search mode, results are cleared
    size_t before = highlightedLine();
    key(ftxui::Event::Character('n'));  // should do nothing
    EXPECT_EQ(highlightedLine(), before);
}

TEST_F(SearchTest, SearchChineseKeyword) {
    TempFile f("第一行\n第二行\n第一个\n");
    LogReader r2;
    r2.open(f.path());
    FilterChain c2(r2);
    AppController ctrl2(r2, c2);
    ctrl2.getViewData(5, 5);

    ctrl2.handleKey(ftxui::Event::Character('/'));
    for (char ch : std::string("第一"))
        ctrl2.handleKey(ftxui::Event::Character(std::string(1, ch)));
    ctrl2.handleKey(ftxui::Event::Return);

    // Should find lines 1 and 3
    size_t hl = 0;
    for (auto& ll : ctrl2.getViewData(5, 5).rawPane)
        if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 1u);
}
