#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using namespace ftxui;

// ── Encoding render tests ─────────────────────────────────────────────────────
// Verify that UTF-16LE / UTF-16BE / UTF-8 BOM files are rendered without the
// characteristic "dot between every character" artefact that appears when a
// UTF-16 file is read as raw bytes (every null byte of the 2-byte code units
// becomes a '.' after control-character sanitisation).

// Build an ASCII string as UTF-16LE bytes (BOM included).
static std::string makeUtf16Le(const char* ascii) {
    std::string out;
    out.push_back('\xFF'); out.push_back('\xFE');
    for (const char* p = ascii; *p; ++p) {
        out.push_back(*p);
        out.push_back('\x00');
    }
    return out;
}

// Build an ASCII string as UTF-16BE bytes (BOM included).
static std::string makeUtf16Be(const char* ascii) {
    std::string out;
    out.push_back('\xFE'); out.push_back('\xFF');
    for (const char* p = ascii; *p; ++p) {
        out.push_back('\x00');
        out.push_back(*p);
    }
    return out;
}

// Render a file to an in-memory screen and return the string buffer.
static std::string renderFile(const std::string& path, int w = 80, int h = 10) {
    LogReader reader;
    reader.open(path);
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    auto si   = ScreenInteractive::TerminalOutput();
    auto comp = CreateMainComponent(ctrl, si);
    ctrl.onTerminalResize(w, h);
    Screen scr = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(scr, comp->Render());
    return scr.ToString();
}

// ── UTF-16LE ──────────────────────────────────────────────────────────────────

// A UTF-16LE file must render readable ASCII text, not "H.e.l.l.o." artefacts.
TEST(EncodingRender, Utf16LeShowsTextWithoutDots) {
    TempFile f(makeUtf16Le("Hello\n"));
    std::string out = renderFile(f.path());

    // The word "Hello" must appear intact in the rendered output.
    EXPECT_NE(out.find("Hello"), std::string::npos)
        << "UTF-16LE file should render 'Hello', not dot-separated characters";

    // There must be NO isolated dots between H and e, e and l, etc.
    // We check by counting dots: a naively-decoded file would produce at least
    // 4 extra dots for "Hello" (H.e.l.l.o).  A correct render has 0 or very
    // few dots (only from line-number / status-bar decoration).
    // We verify that the text "H.e" does not appear (characteristic artefact).
    EXPECT_EQ(out.find("H.e"), std::string::npos)
        << "UTF-16LE decoded incorrectly: found 'H.e' artefact in rendered output";
}

// Multiline UTF-16LE file must produce the correct line count.
// Use a taller screen so all three lines fit inside the log pane.
TEST(EncodingRender, Utf16LeMultilineRenders) {
    TempFile f(makeUtf16Le("line1\nline2\nline3\n"));
    std::string out = renderFile(f.path(), /*w=*/80, /*h=*/20);

    EXPECT_NE(out.find("line1"), std::string::npos);
    EXPECT_NE(out.find("line2"), std::string::npos);
    EXPECT_NE(out.find("line3"), std::string::npos);
}

// ── UTF-16BE ──────────────────────────────────────────────────────────────────

TEST(EncodingRender, Utf16BeShowsTextWithoutDots) {
    TempFile f(makeUtf16Be("World\n"));
    std::string out = renderFile(f.path());

    EXPECT_NE(out.find("World"), std::string::npos)
        << "UTF-16BE file should render 'World' without dot artefacts";
    EXPECT_EQ(out.find("W.o"), std::string::npos)
        << "UTF-16BE decoded incorrectly: found 'W.o' artefact";
}

// ── UTF-8 BOM ─────────────────────────────────────────────────────────────────

// UTF-8 BOM (EF BB BF) must be stripped: the BOM bytes must not appear in the
// rendered text as garbage characters.
TEST(EncodingRender, Utf8BomDoesNotRenderBomBytes) {
    std::string content;
    content.push_back('\xEF'); content.push_back('\xBB'); content.push_back('\xBF');
    content += "clean line\n";
    TempFile f(content);

    std::string out = renderFile(f.path());

    EXPECT_NE(out.find("clean line"), std::string::npos)
        << "UTF-8 BOM file should render content normally";

    // The 3 BOM bytes rendered as control-char dots would produce "..." at the
    // start of the line.  Verify the word "clean" is not preceded by dots in the
    // output by checking the artefact substring does not appear.
    EXPECT_EQ(out.find("...clean"), std::string::npos)
        << "UTF-8 BOM bytes must be stripped, not rendered as '...'";
}
