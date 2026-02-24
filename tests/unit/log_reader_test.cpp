#include <gtest/gtest.h>
#include "log_reader.hpp"
#include "temp_file.hpp"

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string sv(std::string_view sv) { return std::string(sv); }

// ── Open / close ───────────────────────────────────────────────────────────────

TEST(LogReader, OpenValidFile) {
    TempFile f("hello\nworld\n");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_FALSE(reader.isIndexing());
}

TEST(LogReader, OpenNonExistentFile) {
    LogReader reader;
    EXPECT_FALSE(reader.open("/does/not/exist/file.log"));
    EXPECT_EQ(reader.lineCount(), 0u);
}

TEST(LogReader, OpenTwiceWithoutClose) {
    TempFile f1("line1\n");
    TempFile f2("line2\nline3\n");
    LogReader reader;
    EXPECT_TRUE(reader.open(f1.path()));
    EXPECT_EQ(reader.lineCount(), 1u);
    // Second open should close the first and open the new one
    EXPECT_TRUE(reader.open(f2.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
}

TEST(LogReader, CloseResetsState) {
    TempFile f("hello\n");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    reader.close();
    EXPECT_EQ(reader.lineCount(), 0u);
}

// ── Empty file ─────────────────────────────────────────────────────────────────

TEST(LogReader, EmptyFile) {
    TempFile f("");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 0u);
    EXPECT_EQ(sv(reader.getLine(1)), "");
}

// ── Line count ─────────────────────────────────────────────────────────────────

TEST(LogReader, SingleLineNoNewline) {
    TempFile f("hello");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
}

TEST(LogReader, SingleLineWithNewline) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
}

TEST(LogReader, MultipleLines) {
    TempFile f("one\ntwo\nthree\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 3u);
    EXPECT_EQ(sv(reader.getLine(1)), "one");
    EXPECT_EQ(sv(reader.getLine(2)), "two");
    EXPECT_EQ(sv(reader.getLine(3)), "three");
}

TEST(LogReader, OnlyNewline) {
    TempFile f("\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    // One empty line
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "");
}

TEST(LogReader, TwoNewlines) {
    TempFile f("\n\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
}

// ── CRLF handling ─────────────────────────────────────────────────────────────

TEST(LogReader, CRLFFile) {
    TempFile f("hello\r\nworld\r\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
    EXPECT_EQ(sv(reader.getLine(2)), "world");
}

TEST(LogReader, MixedLineEndings) {
    TempFile f("lf\ncrlf\r\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "lf");
    EXPECT_EQ(sv(reader.getLine(2)), "crlf");
}

// ── getLine boundary ──────────────────────────────────────────────────────────

TEST(LogReader, GetLineOutOfRangeZero) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    // lineNo=0 is invalid (1-based)
    EXPECT_EQ(sv(reader.getLine(0)), "");
}

TEST(LogReader, GetLineOutOfRangeHigh) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    // lineNo > lineCount
    EXPECT_EQ(sv(reader.getLine(99)), "");
}

// ── getLines ─────────────────────────────────────────────────────────────────

TEST(LogReader, GetLinesNormal) {
    TempFile f("a\nb\nc\nd\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    auto lines = reader.getLines(2, 3);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(sv(lines[0]), "b");
    EXPECT_EQ(sv(lines[1]), "c");
}

TEST(LogReader, GetLinesFromGtTo) {
    TempFile f("a\nb\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_TRUE(reader.getLines(3, 2).empty());
}

TEST(LogReader, GetLinesClampedToEnd) {
    TempFile f("a\nb\nc\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    auto lines = reader.getLines(2, 100);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(sv(lines[0]), "b");
    EXPECT_EQ(sv(lines[1]), "c");
}

// ── UTF-8 ─────────────────────────────────────────────────────────────────────

TEST(LogReader, Utf8ChineseContent) {
    // "你好\n世界\n" – Chinese characters, 3 bytes each in UTF-8
    TempFile f("\xe4\xbd\xa0\xe5\xa5\xbd\n\xe4\xb8\x96\xe7\x95\x8c\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(reader.lineCount(), 2u);
    // Content should be intact (not truncated mid-character)
    auto line1 = reader.getLine(1);
    EXPECT_EQ(line1.size(), 6u);  // 2 × 3-byte chars
}

// ── filePath ──────────────────────────────────────────────────────────────────

TEST(LogReader, FilePath) {
    TempFile f("x\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_EQ(sv(reader.filePath()), f.path());
}

// ── forceCheck (Phase 1 no-op) ───────────────────────────────────────────────

TEST(LogReader, ForceCheckNoop) {
    TempFile f("x\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    EXPECT_NO_THROW(reader.forceCheck());
}

TEST(LogReader, ForceCheckWithoutOpen) {
    LogReader reader;
    EXPECT_NO_THROW(reader.forceCheck());
}
