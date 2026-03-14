#include <gtest/gtest.h>
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string sv(std::string_view sv) { return std::string(sv); }

// ── Open / close ───────────────────────────────────────────────────────────────

TEST(LogReader, OpenValidFile) {
    TempFile f("hello\nworld\n");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
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
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 1u);
    // Second open should close the first and open the new one
    EXPECT_TRUE(reader.open(f2.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 2u);
}

TEST(LogReader, CloseResetsState) {
    TempFile f("hello\n");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    reader.close();
    EXPECT_EQ(reader.lineCount(), 0u);
}

// ── Empty file ─────────────────────────────────────────────────────────────────

TEST(LogReader, EmptyFile) {
    TempFile f("");
    LogReader reader;
    EXPECT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 0u);
    EXPECT_EQ(sv(reader.getLine(1)), "");
}

// ── Line count ─────────────────────────────────────────────────────────────────

TEST(LogReader, SingleLineNoNewline) {
    TempFile f("hello");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
}

TEST(LogReader, SingleLineWithNewline) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
}

TEST(LogReader, MultipleLines) {
    TempFile f("one\ntwo\nthree\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 3u);
    EXPECT_EQ(sv(reader.getLine(1)), "one");
    EXPECT_EQ(sv(reader.getLine(2)), "two");
    EXPECT_EQ(sv(reader.getLine(3)), "three");
}

TEST(LogReader, OnlyNewline) {
    TempFile f("\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    // One empty line
    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "");
}

TEST(LogReader, TwoNewlines) {
    TempFile f("\n\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 2u);
}

// ── CRLF handling ─────────────────────────────────────────────────────────────

TEST(LogReader, CRLFFile) {
    TempFile f("hello\r\nworld\r\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
    EXPECT_EQ(sv(reader.getLine(2)), "world");
}

TEST(LogReader, MixedLineEndings) {
    TempFile f("lf\ncrlf\r\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "lf");
    EXPECT_EQ(sv(reader.getLine(2)), "crlf");
}

// ── getLine boundary ──────────────────────────────────────────────────────────

TEST(LogReader, GetLineOutOfRangeZero) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    // lineNo=0 is invalid (1-based)
    EXPECT_EQ(sv(reader.getLine(0)), "");
}

TEST(LogReader, GetLineOutOfRangeHigh) {
    TempFile f("hello\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    // lineNo > lineCount
    EXPECT_EQ(sv(reader.getLine(99)), "");
}

// ── getLines ─────────────────────────────────────────────────────────────────

TEST(LogReader, GetLinesNormal) {
    TempFile f("a\nb\nc\nd\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    auto lines = reader.getLines(2, 3);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(sv(lines[0]), "b");
    EXPECT_EQ(sv(lines[1]), "c");
}

TEST(LogReader, GetLinesFromGtTo) {
    TempFile f("a\nb\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_TRUE(reader.getLines(3, 2).empty());
}

TEST(LogReader, GetLinesClampedToEnd) {
    TempFile f("a\nb\nc\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
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
    waitForIndexing(reader);
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
    waitForIndexing(reader);
    EXPECT_EQ(sv(reader.filePath()), f.path());
}

// ── forceCheck (Phase 1 no-op) ───────────────────────────────────────────────

TEST(LogReader, ForceCheckNoop) {
    TempFile f("x\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    EXPECT_NO_THROW(reader.forceCheck());
}

TEST(LogReader, ForceCheckWithoutOpen) {
    LogReader reader;
    EXPECT_NO_THROW(reader.forceCheck());
}

// ── Phase 3: close() / destructor safety ─────────────────────────────────────

TEST(LogReader, CloseImmediatelyAfterOpen) {
    // open() kicks off IndexThread; close() must not crash
    TempFile f("a\nb\nc\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    // Do NOT wait for indexing – close immediately to exercise concurrent path
    EXPECT_NO_THROW(reader.close());
    EXPECT_EQ(reader.lineCount(), 0u);
}

TEST(LogReader, DestructorSafeWhileIndexing) {
    // Destructor should join IndexThread even if it hasn't finished
    TempFile f("a\nb\nc\n");
    {
        LogReader reader;
        reader.open(f.path());
        // Reader goes out of scope without waiting – destructor must be safe
    }
    // If we reach here without hanging or crashing, the test passes
    SUCCEED();
}

TEST(LogReader, StaticModeForceCheckDetectsNewLines) {
    TempFile f("line1\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);
    reader.setMode(FileMode::Static);

    // Append without switching to realtime
    size_t newLinesSeen = 0;
    reader.onNewLines([&](size_t, size_t) { ++newLinesSeen; });

    f.append("line2\n");
    reader.forceCheck();

    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_GT(newLinesSeen, 0u);
}

// ── Encoding detection ─────────────────────────────────────────────────────────
//
// UTF-16LE helper: encode an ASCII string as UTF-16LE bytes (BOM included).
// Each ASCII char becomes two bytes: char, 0x00.  Newlines likewise.
static std::string makeUtf16Le(std::string_view ascii) {
    std::string out;
    out.reserve(2 + ascii.size() * 2);
    out.push_back('\xFF'); out.push_back('\xFE');  // BOM
    for (unsigned char c : ascii) {
        out.push_back(static_cast<char>(c));
        out.push_back('\x00');
    }
    return out;
}

// UTF-16BE helper: BOM + big-endian bytes.
static std::string makeUtf16Be(std::string_view ascii) {
    std::string out;
    out.reserve(2 + ascii.size() * 2);
    out.push_back('\xFE'); out.push_back('\xFF');  // BOM
    for (unsigned char c : ascii) {
        out.push_back('\x00');
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// UTF-8 BOM helper.
static std::string makeUtf8Bom(std::string_view content) {
    std::string out;
    out.push_back('\xEF'); out.push_back('\xBB'); out.push_back('\xBF');
    out.append(content);
    return out;
}

TEST(LogReaderEncoding, Utf16LeDetectedAndDecoded) {
    TempFile f(makeUtf16Le("hello\nworld\n"));
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.detectedEncoding(), FileEncoding::Utf16Le);
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "hello");
    EXPECT_EQ(sv(reader.getLine(2)), "world");
}

TEST(LogReaderEncoding, Utf16BeDetectedAndDecoded) {
    TempFile f(makeUtf16Be("foo\nbar\n"));
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.detectedEncoding(), FileEncoding::Utf16Be);
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "foo");
    EXPECT_EQ(sv(reader.getLine(2)), "bar");
}

TEST(LogReaderEncoding, Utf8BomStripped) {
    TempFile f(makeUtf8Bom("line1\nline2\n"));
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    // Encoding is normalised to Utf8; BOM must not appear in line content.
    EXPECT_EQ(reader.detectedEncoding(), FileEncoding::Utf8);
    EXPECT_EQ(reader.lineCount(), 2u);
    EXPECT_EQ(sv(reader.getLine(1)), "line1");
    EXPECT_EQ(sv(reader.getLine(2)), "line2");
}

TEST(LogReaderEncoding, PlainUtf8NoBom) {
    TempFile f("alpha\nbeta\n");
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.detectedEncoding(), FileEncoding::Utf8);
    EXPECT_EQ(reader.lineCount(), 2u);
}

TEST(LogReaderEncoding, Utf16LeCjkCharacters) {
    // "日志" in UTF-16LE: U+65E5=\xE5\x65\x00, U+5FD7=\xD7\x5F\x00
    // Easier to build: use the actual UTF-16LE encoding of the CJK string.
    // "日" = U+65E5 → LE bytes: 0xE5 0x65
    // "志" = U+5FD7 → LE bytes: 0xD7 0x5F
    // "\n" = U+000A → LE bytes: 0x0A 0x00
    std::string raw;
    raw.push_back('\xFF'); raw.push_back('\xFE');  // BOM
    // 日 (U+65E5)
    raw.push_back('\xE5'); raw.push_back('\x65');
    // 志 (U+5FD7)
    raw.push_back('\xD7'); raw.push_back('\x5F');
    // newline
    raw.push_back('\x0A'); raw.push_back('\x00');

    TempFile f(raw);
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.detectedEncoding(), FileEncoding::Utf16Le);
    EXPECT_EQ(reader.lineCount(), 1u);
    // The decoded UTF-8 for U+65E5 U+5FD7 is "\xE6\x97\xA5\xE5\xBF\x97"
    EXPECT_EQ(sv(reader.getLine(1)), "\xE6\x97\xA5\xE5\xBF\x97");
}

TEST(LogReaderEncoding, Utf16LeNoTrailingNewline) {
    // Single line without trailing newline.
    TempFile f(makeUtf16Le("only"));
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "only");
}

TEST(LogReaderEncoding, Utf16LeMultipleLines) {
    TempFile f(makeUtf16Le("a\nb\nc\n"));
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.lineCount(), 3u);
    EXPECT_EQ(sv(reader.getLine(1)), "a");
    EXPECT_EQ(sv(reader.getLine(2)), "b");
    EXPECT_EQ(sv(reader.getLine(3)), "c");
}

TEST(LogReaderEncoding, Utf16LeSurrogatePair) {
    // U+1F600 (GRINNING FACE emoji) encoded as UTF-16LE surrogate pair:
    //   high surrogate: U+D83D → LE: 0x3D 0xD8
    //   low  surrogate: U+DE00 → LE: 0x00 0xDE
    // UTF-8: 0xF0 0x9F 0x98 0x80
    std::string raw;
    raw.push_back('\xFF'); raw.push_back('\xFE');  // BOM
    raw.push_back('\x3D'); raw.push_back('\xD8');  // high surrogate
    raw.push_back('\x00'); raw.push_back('\xDE');  // low surrogate
    raw.push_back('\x0A'); raw.push_back('\x00');  // newline

    TempFile f(raw);
    LogReader reader;
    ASSERT_TRUE(reader.open(f.path()));
    waitForIndexing(reader);

    EXPECT_EQ(reader.lineCount(), 1u);
    EXPECT_EQ(sv(reader.getLine(1)), "\xF0\x9F\x98\x80");
}
