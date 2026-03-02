#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using namespace ftxui;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Count pixels whose .character field equals the given string.
static int countPixelChar(const Screen& scr, const std::string& ch) {
    int n = 0;
    for (int y = 0; y < scr.dimy(); ++y)
        for (int x = 0; x < scr.dimx(); ++x)
            if (scr.PixelAt(x, y).character == ch)
                ++n;
    return n;
}

// Render the file at filePath to a fixed-size in-memory screen.
static Screen renderFile(const std::string& filePath, int w = 80, int h = 10) {
    LogReader reader;
    reader.open(filePath);
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    auto si   = ScreenInteractive::TerminalOutput();
    auto comp = CreateMainComponent(ctrl, si);
    ctrl.onTerminalResize(w, h);
    Screen scr = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(scr, comp->Render());
    return scr;
}

// ── Control character sanitization render tests ──────────────────────────────
// Verify that ASCII control characters (0x00-0x1F, 0x7F) embedded in log
// content are replaced by '.' before reaching the terminal.  Without this
// sanitization a raw \r (0x0D) mid-line would move the terminal cursor to
// column 0 and overwrite the beginning of the display.

TEST(ControlCharRender, SohReplacedByDot) {
    // SOH (0x01) embedded in a log line must render as '.' not raw SOH.
    TempFile f("alpha\x01" "beta\n");
    Screen scr = renderFile(f.path());

    EXPECT_EQ(countPixelChar(scr, "\x01"), 0)
        << "SOH (0x01) should never appear as a rendered pixel";
    EXPECT_GT(countPixelChar(scr, "."), 0)
        << "Sanitized control char should render as '.'";
}

TEST(ControlCharRender, CrReplacedByDot) {
    // CR (0x0D) embedded mid-line is the most critical case: if it reaches the
    // terminal as a raw byte the cursor jumps to column 0 and corrupts the line.
    TempFile f("foo\rbar\n");  // \r inside the line, \n is the line terminator
    Screen scr = renderFile(f.path());

    // The \r pixel must be replaced; no raw CR should appear in any pixel.
    EXPECT_EQ(countPixelChar(scr, "\r"), 0)
        << "CR (0x0D) mid-line should never appear as a rendered pixel";
    // The replacement dot must be visible.
    EXPECT_GT(countPixelChar(scr, "."), 0)
        << "Sanitized CR should render as '.'";
}

TEST(ControlCharRender, DelReplacedByDot) {
    // DEL (0x7F) is included in the sanitization range.
    TempFile f("test\x7F" "end\n");
    Screen scr = renderFile(f.path());

    EXPECT_EQ(countPixelChar(scr, "\x7F"), 0)
        << "DEL (0x7F) should never appear as a rendered pixel";
    EXPECT_GT(countPixelChar(scr, "."), 0)
        << "Sanitized DEL should render as '.'";
}

TEST(ControlCharRender, NulByteReplacedByDot) {
    // NUL (0x00) mid-line: use explicit size constructor to avoid string truncation.
    TempFile f(std::string("nul\x00""byte\n", 9));  // 9 bytes: nul + \x00 + byte + \n
    Screen scr = renderFile(f.path());

    EXPECT_EQ(countPixelChar(scr, std::string(1, '\0')), 0)
        << "NUL (0x00) should never appear as a rendered pixel";
}

TEST(ControlCharRender, MultipleControlCharsInOneLine) {
    // Three different control chars in one line: each becomes '.'.
    // Line content: a SOH b CR c DEL d   →  a.b.c.d
    TempFile f("a\x01" "b\x0D" "c\x7F" "d\n");
    Screen scr = renderFile(f.path());

    EXPECT_EQ(countPixelChar(scr, "\x01"), 0) << "SOH must not appear";
    EXPECT_EQ(countPixelChar(scr, "\r"),   0) << "CR must not appear";
    EXPECT_EQ(countPixelChar(scr, "\x7F"), 0) << "DEL must not appear";
    // All three control chars → three '.' pixels (there may be more from gutter)
    EXPECT_GE(countPixelChar(scr, "."), 3)
        << "Each sanitized control char should produce one '.' pixel";
}

TEST(ControlCharRender, SurroundingContentPreserved) {
    // Visible text around the control char must still appear intact.
    TempFile f("hello\x01" "world\n");
    Screen scr = renderFile(f.path());
    std::string out = scr.ToString();

    EXPECT_NE(out.find("hello"), std::string::npos)
        << "Content before control char should be preserved";
    EXPECT_NE(out.find("world"), std::string::npos)
        << "Content after control char should be preserved";
}

TEST(ControlCharRender, NormalLineUnaffected) {
    // Lines with only printable ASCII must pass through completely unchanged.
    TempFile f("normal line\n");
    Screen scr = renderFile(f.path());
    std::string out = scr.ToString();

    EXPECT_NE(out.find("normal line"), std::string::npos)
        << "Normal ASCII content should render verbatim";
}

TEST(ControlCharRender, AllControlCharsInRange) {
    // Verify the full 0x01–0x1F range (excluding 0x0A which is the line terminator).
    // Build a line that contains bytes 0x01-0x09, 0x0B-0x1F (29 bytes) + newline.
    std::string content;
    for (int b = 0x01; b <= 0x1F; ++b) {
        if (b == 0x0A) continue;  // skip LF — it is the line terminator
        content += static_cast<char>(b);
    }
    content += '\n';
    TempFile f(content);
    Screen scr = renderFile(f.path());

    // None of the raw control bytes should survive into the rendered pixels.
    for (int b = 0x01; b <= 0x1F; ++b) {
        if (b == 0x0A) continue;
        EXPECT_EQ(countPixelChar(scr, std::string(1, static_cast<char>(b))), 0)
            << "Control byte 0x" << std::hex << b << " should not appear in output";
    }
}
