// apex-vt-engine-ext tests — v0.2 foundation API coverage.
//
// Covers the engine-level wiring of the foundation modules: mouse mode
// negotiation + encode, focus reporting, DECKPAM, modifyOtherKeys/Kitty,
// DECRQM responses, DECSED/DECSEL/DECSCA protection, rectangular ops,
// XTPUSHSGR, OSC 8 hyperlinks (span attach + invalidation), sync mode 2026,
// search (cross-wrap, global rows), selection (word/line + text), session
// save/restore roundtrip, reflow content preservation, and wheel routing.
#include "apex/vt/vt_engine.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "mini_test.h"
#include "vt_input.h"

using apex::vt::Engine;
using apex::vt::MutationType;
using apex::vt::SearchMatchInfo;

namespace {

void feed(Engine& e, const char* s) { e.feed(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }

std::string vec(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

bool contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

// ═══ Mouse mode negotiation + event encoding ═══

MINI_TEST(mouse_modes_default_off) {
  Engine e(5, 20);
  CHECK(e.encodeMouseEvent(0, 0, 0, 3, 3).empty());  // off → nothing reportable
  CHECK(e.encodeMouseEvent(1, 0, 0, 3, 3).empty());
}

MINI_TEST(mouse_sgr_roundtrip) {
  Engine e(5, 20);
  feed(e, "\x1b[?1000h\x1b[?1006h");  // normal tracking + SGR
  std::string press = vec(e.encodeMouseEvent(0, 0, 0, 3, 7));
  CHECK_EQ(press, std::string("\x1b[<0;3;7M"));
  std::string release = vec(e.encodeMouseEvent(1, 0, 0, 3, 7));
  CHECK_EQ(release, std::string("\x1b[<0;3;7m"));
  // mode off again → silent
  feed(e, "\x1b[?1000l");
  CHECK(e.encodeMouseEvent(0, 0, 0, 3, 7).empty());
}

MINI_TEST(mouse_x11_encoding) {
  Engine e(5, 20);
  feed(e, "\x1b[?1000h");  // normal + default X11 encoding
  std::string press = vec(e.encodeMouseEvent(0, 0, 0, 1, 1));
  // ESC [ M Cb Cx Cy with Cb=32, Cx=32, Cy=32
  CHECK_EQ(press.size(), size_t(6));
  CHECK_EQ(press[0], char(0x1B));
  CHECK_EQ(press[1], '[');
  CHECK_EQ(press[2], 'M');
  CHECK_EQ(int(uint8_t(press[3])), 32);
  CHECK_EQ(int(uint8_t(press[4])), 32);
  CHECK_EQ(int(uint8_t(press[5])), 32);
}

MINI_TEST(mouse_mode_1002_button_tracking) {
  Engine e(5, 20);
  feed(e, "\x1b[?1002h");
  CHECK(!e.encodeMouseEvent(2, 0, 0, 2, 2).empty());     // motion w/ button
  CHECK(e.encodeMouseEvent(2, -1, 0, 2, 2).empty());     // buttonless motion gated
  feed(e, "\x1b[?1003h");
  CHECK(!e.encodeMouseEvent(2, -1, 0, 2, 2).empty());    // any-event: motion ok
}

// ═══ Focus reporting / keypad / modifyOtherKeys ═══

MINI_TEST(focus_reporting_gated_by_1004) {
  Engine e(5, 20);
  CHECK(e.encodeFocusEvent(true).empty());
  feed(e, "\x1b[?1004h");
  CHECK_EQ(vec(e.encodeFocusEvent(true)), std::string("\x1b[I"));
  CHECK_EQ(vec(e.encodeFocusEvent(false)), std::string("\x1b[O"));
  feed(e, "\x1b[?1004l");
  CHECK(e.encodeFocusEvent(true).empty());
}

MINI_TEST(deckpam_via_esc_equals) {
  Engine e(5, 20);
  CHECK_EQ(vec(e.encodeKey(int(apex::vt::input::Key::kNumpad5), 0)), std::string("5"));
  feed(e, "\x1b=");  // DECKPAM
  CHECK_EQ(vec(e.encodeKey(int(apex::vt::input::Key::kNumpad5), 0)), std::string("\x1bOu"));
  feed(e, "\x1b>");  // DECKPNM
  CHECK_EQ(vec(e.encodeKey(int(apex::vt::input::Key::kNumpad5), 0)), std::string("5"));
}

MINI_TEST(modify_other_keys_negotiation) {
  Engine e(5, 20);
  // Default: ctrl+enter → CR? No: plain Enter encodes CR (legacy).
  CHECK_EQ(vec(e.encodeKey(int(apex::vt::input::Key::kEnter), 0)), std::string("\r"));
  feed(e, "\x1b[>4;2c");  // modifyOtherKeys = 2
  std::string ctrlEnter = vec(e.encodeKey(int(apex::vt::input::Key::kEnter), 4));
  CHECK(contains(ctrlEnter.c_str(), "27;") || !ctrlEnter.empty());
  // Kitty: CSI = 1;1 u enables
  feed(e, "\x1b[=1;1u");
  std::string kitty = vec(e.encodeKey(int(apex::vt::input::Key::kEnter), 4));
  CHECK(!kitty.empty());
  feed(e, "\x1b[=1;0u");  // disable
  CHECK_EQ(vec(e.encodeKey(int(apex::vt::input::Key::kEnter), 0)), std::string("\r"));
}

// ═══ DECRQM ═══

MINI_TEST(decrqm_reports_mode_states) {
  Engine e(5, 20);
  feed(e, "\x1b[?1h");         // DECCKM on
  feed(e, "\x1b[?1$p");        // query mode 1
  auto resp = e.pollResponses();
  std::string r(resp.begin(), resp.end());
  CHECK_EQ(r, std::string("\x1b[?1;1$y"));
  feed(e, "\x1b[?7$p");  // DECAWM (default on)
  resp = e.pollResponses();
  r.assign(resp.begin(), resp.end());
  CHECK_EQ(r, std::string("\x1b[?7;1$y"));
  feed(e, "\x1b[?9999$p");  // unknown mode
  resp = e.pollResponses();
  r.assign(resp.begin(), resp.end());
  CHECK_EQ(r, std::string("\x1b[?9999;0$y"));
}

// ═══ DECSED / DECSEL / DECSCA protection ═══

MINI_TEST(protected_cells_survive_decsel) {
  Engine e(3, 10);
  feed(e, "abc");
  feed(e, "\x1b[1\"q");   // DECSCA: protected ON
  feed(e, "\x1b[1;4H");   // cursor to (1,4)
  feed(e, "XYZ");         // protected chars
  feed(e, "\x1b[0\"q");   // protected OFF
  feed(e, "\x1b[1;3H");
  feed(e, "\x1b[?K");     // DECSEL 0: erase from cursor to EOL (selective)
  auto ls = splitLines(e.renderedText());
  // 'c' (unprotected) blanks; the protected XYZ survive. Erase writes a
  // blank cell (bg-persistence) — the column itself stays.
  CHECK_EQ(ls[0], std::string("ab XYZ"));
  // Plain EL 2 erases everything including protected
  feed(e, "\x1b[2K");
  CHECK_EQ(splitLines(e.renderedText())[0], std::string(""));
}

MINI_TEST(decsed_protects_across_screen) {
  Engine e(3, 10);
  feed(e, "\x1b[1\"qP\x1b[0\"q");  // protected 'P' at (0,0)
  feed(e, "\x1b[?2J");              // DECSED 2 — selective full erase
  CHECK_EQ(splitLines(e.renderedText())[0], std::string("P"));
}

// ═══ Rectangular operations ═══

MINI_TEST(decera_erases_rectangle) {
  Engine e(4, 10);
  feed(e, "aaaaaaaaaa\x1b[2;1Hbbbbbbbbbb\x1b[3;1Hcccccccccc");
  feed(e, "\x1b[2;3;3;6$z");  // DECERA: rows 2-3, cols 3-6 (1-based → cols 2..5)
  auto ls = splitLines(e.renderedText());
  CHECK_EQ(ls[1], std::string("bb    bbbb"));
  CHECK_EQ(ls[2], std::string("cc    cccc"));
  CHECK_EQ(ls[0], std::string("aaaaaaaaaa"));  // row 1 untouched
}

MINI_TEST(decsera_respects_protection) {
  Engine e(3, 10);
  feed(e, "\x1b[1\"qPPPP\x1b[0\"q12345");
  feed(e, "\x1b[1;1;1;1;5${");  // DECSERA over cols 1-5
  CHECK_EQ(splitLines(e.renderedText())[0], std::string("PPPP12345"));
}

MINI_TEST(decfra_fills_rectangle) {
  Engine e(4, 10);
  feed(e, "\x1b[35;2;2;3;6$x");  // fill 'X' (0x58? no — 35 = '#'): use '#' (0x23)
  // '#' = 35 decimal → fill char '#', rows 2-3, cols 2-6
  auto ls = splitLines(e.renderedText());
  CHECK_EQ(ls[1], std::string(" #####"));
  CHECK_EQ(ls[2], std::string(" #####"));
}

MINI_TEST(deccra_copies_rectangle) {
  Engine e(4, 10);
  feed(e, "ABCDEFGHIJ");
  feed(e, "\x1b[1;1;1;4;3;7$v");  // copy rows1/cols1-4 → row 3, col 7
  auto ls = splitLines(e.renderedText());
  CHECK_EQ(ls[2], std::string("      ABCD"));
}

// ═══ XTPUSHSGR / XTPOPSGR / XTREPORTSGR ═══

MINI_TEST(sgr_stack_push_pop) {
  Engine e(3, 10);
  feed(e, "\x1b[1m");      // bold
  feed(e, "\x1b[#{");      // XTPUSHSGR
  feed(e, "\x1b[0m\x1b[32m");  // reset + green
  feed(e, "G");
  feed(e, "\x1b[#}");     // XTPOPSGR → bold restored
  feed(e, "B");
  // Verify via snapshot flags of the two cells.
  auto snap = e.renderSnapshot(0);
  bool boldG = (snap.visible[0].cells[0].flags & apex::vt::kRfBold) != 0;
  bool boldB = (snap.visible[0].cells[1].flags & apex::vt::kRfBold) != 0;
  CHECK(!boldG);
  CHECK(boldB);
}

MINI_TEST(xtreportsgr_responds) {
  Engine e(3, 10);
  feed(e, "\x1b[1m\x1b[38;2;10;20;30m");
  feed(e, "\x1b[#~");
  auto resp = e.pollResponses();
  std::string r(resp.begin(), resp.end());
  CHECK(!r.empty());
  CHECK_EQ(r.substr(0, 4), std::string("\x1b[#;"));
  CHECK(contains(r, ";1;"));       // bold
  CHECK(contains(r, "38;2;10;20;30"));  // truecolor
}

// ═══ OSC 8 hyperlinks ═══

MINI_TEST(hyperlink_attach_and_query) {
  Engine e(3, 40);
  feed(e, "\x1b]8;;https://example.com\x07");
  feed(e, "click me");
  feed(e, "\x1b]8;;\x07");
  CHECK(e.linkAt(0, 0) != nullptr);
  CHECK_EQ(std::string(e.linkAt(0, 2)), std::string("https://example.com"));
  CHECK(e.linkAt(0, 7) != nullptr);
  CHECK(e.linkAt(0, 8) == nullptr);  // past the span
  CHECK(e.linkAt(1, 0) == nullptr);  // other row
}

MINI_TEST(hyperlink_visible_in_snapshot) {
  Engine e(3, 40);
  feed(e, "\x1b]8;;file:///tmp/a.txt\x07" "hi" "\x1b]8;;\x07");
  auto snap = e.renderSnapshot(0);
  CHECK_EQ(snap.links.size(), size_t(1));
  CHECK_EQ(snap.links[0], std::string("file:///tmp/a.txt"));
  CHECK_EQ(snap.visible[0].cells.size(), size_t(2));  // "hi" — trailing trim
  CHECK_EQ(snap.visible[0].cells[0].link, uint16_t(1));
  CHECK_EQ(snap.visible[0].cells[1].link, uint16_t(1));
}

MINI_TEST(hyperlink_invalidation_on_overwrite) {
  Engine e(3, 40);
  feed(e, "\x1b]8;;https://x.io\x07" "abc" "\x1b]8;;\x07");
  CHECK(e.linkAt(0, 1) != nullptr);
  feed(e, "\x1b[1;2HZ");  // overwrite cell (0,1)
  CHECK(e.linkAt(0, 0) != nullptr);
  CHECK(e.linkAt(0, 1) == nullptr);  // span trimmed at the overwritten cell
  CHECK(e.linkAt(0, 2) != nullptr);
}

MINI_TEST(hyperlink_bounded_table) {
  Engine e(3, 40);
  for (int i = 0; i < 80; ++i) {
    char osc[64];
    std::snprintf(osc, sizeof(osc), "\x1b]8;;https://x/%d\x07" "\x1b[H" "L" "\x1b]8;;\x07", i);
    feed(e, osc);
    if (i != 79) feed(e, "\x1b[2J");  // clear between rounds (drops spans)
  }
  // Table bounded at 64; no crash, the LAST link still resolves at (0,0).
  CHECK(e.linkAt(0, 0) != nullptr);
  CHECK_EQ(std::string(e.linkAt(0, 0)), std::string("https://x/79"));
}

// ═══ Sync output mode 2026 ═══

MINI_TEST(sync_mode_folds_mutations) {
  Engine e(3, 10);
  feed(e, "\x1b[?2026h");  // begin sync
  feed(e, "aaa\x1b[2;1Hbbb");
  auto m = e.drainMutations();
  CHECK(m.empty());  // nothing escapes while syncing
  feed(e, "\x1b[?2026l");  // end sync
  m = e.drainMutations();
  CHECK_EQ(m.size(), size_t(1));
  CHECK(m[0].type == MutationType::kFull);
}

// ═══ OSC color queries ═══

MINI_TEST(osc_color_queries_respond) {
  Engine e(3, 10);
  feed(e, "\x1b]10;?\x07");
  feed(e, "\x1b]11;?\x07");
  feed(e, "\x1b]4;1;?\x07");
  auto resp = e.pollResponses();
  std::string r(resp.begin(), resp.end());
  CHECK_EQ(r.substr(0, 5), std::string("\x1b]10;"));
  CHECK(contains(r, "rgb:"));
  CHECK(contains(r, "\x1b]11;"));
  CHECK(contains(r, "\x1b]4;1;"));
}

// ═══ Search ═══

MINI_TEST(search_finds_screen_text) {
  Engine e(5, 20);
  feed(e, "hello world\r\nsecond LINE");
  CHECK_EQ(e.search("hello", true, false), 1);
  SearchMatchInfo hit = e.searchHit(0);
  // Global row: 0 == first screen row (no scrollback yet)
  CHECK_EQ(hit.startRow, int64_t(0));
  CHECK_EQ(hit.startCol, 0);
  CHECK_EQ(hit.endCol, 5);
}

MINI_TEST(search_case_insensitive) {
  Engine e(5, 20);
  feed(e, "SeCoNd LiNe");
  CHECK_EQ(e.search("second", true, false), 1);
  CHECK_EQ(e.search("second", false, false), 0);  // case-sensitive miss
}

MINI_TEST(search_across_wrapped_rows) {
  Engine e(4, 5);
  feed(e, "abcdefgh");  // wraps: "abcde" / "fgh"
  CHECK_EQ(e.search("cdef", true, false), 1);  // pattern crosses the wrap seam
  SearchMatchInfo hit = e.searchHit(0);
  CHECK_EQ(hit.startRow, int64_t(0));
  CHECK_EQ(hit.startCol, 2);
  CHECK_EQ(hit.endRow, int64_t(1));
  CHECK_EQ(hit.endCol, 1);
}

MINI_TEST(search_whole_word) {
  Engine e(5, 20);
  feed(e, "cat catalog concat");
  CHECK_EQ(e.search("cat", true, false), 3);   // substring hits
  CHECK_EQ(e.search("cat", true, true), 1);    // whole-word only
}

MINI_TEST(search_scrollback_and_global_rows) {
  Engine e(3, 10, 100);
  for (int i = 0; i < 6; ++i) {  // scroll several lines into history
    char buf[32];
    std::snprintf(buf, sizeof(buf), "line%d\r\n", i);
    feed(e, buf);
  }
  feed(e, "done");
  CHECK_EQ(e.search("line", true, false), 6);
  SearchMatchInfo first = e.searchHit(0);
  CHECK_EQ(first.startRow, int64_t(0));  // global row of "line0"
  CHECK_EQ(e.search("done", true, false), 1);  // replaces the hit list
  SearchMatchInfo doneHit = e.searchHit(0);
  CHECK_EQ(doneHit.startRow, int64_t(6));  // screen row 2 → base+sb+2
  CHECK_EQ(doneHit.startCol, 0);
}

MINI_TEST(search_clears_on_resize) {
  Engine e(5, 20);
  feed(e, "needle");
  CHECK_EQ(e.search("needle", true, false), 1);
  e.resize(5, 24);
  CHECK_EQ(e.searchHitCount(), 0);
}

MINI_TEST(active_search_hit_cursor) {
  Engine e(5, 20);
  feed(e, "alpha beta");
  CHECK_EQ(e.search("a", true, false), 3);
  CHECK_EQ(e.activeSearchHit(), -1);
  e.setActiveSearchHit(1);
  CHECK_EQ(e.activeSearchHit(), 1);
  e.setActiveSearchHit(99);  // out of range — ignored
  CHECK_EQ(e.activeSearchHit(), 1);
  e.clearSearch();
  CHECK_EQ(e.searchHitCount(), 0);
  CHECK_EQ(e.activeSearchHit(), -1);
}

// ═══ Selection ═══

MINI_TEST(selection_text_simple) {
  Engine e(3, 20);
  feed(e, "hello world");
  int64_t base = 0;  // no scrollback: global row 0 == screen row 0
  e.beginSelection(base, 0);
  e.extendSelection(base, 5);
  CHECK_EQ(e.selectionText(), std::string("hello"));
}

MINI_TEST(selection_text_reversed_drag) {
  Engine e(3, 20);
  feed(e, "hello world");
  e.beginSelection(0, 5);
  e.extendSelection(0, 0);  // drag backwards
  CHECK_EQ(e.selectionText(), std::string("hello"));
}

MINI_TEST(selection_wrapped_rows_join_without_newline) {
  Engine e(4, 5);
  feed(e, "abcdefgh");
  e.beginSelection(0, 2);
  e.extendSelection(1, 1);
  CHECK_EQ(e.selectionText(), std::string("cdef"));  // wrap seam: no separator
}

MINI_TEST(selection_hard_lines_join_with_newline) {
  Engine e(4, 10);
  feed(e, "ab\r\ncd");
  e.beginSelection(0, 0);
  e.extendSelection(1, 2);
  CHECK_EQ(e.selectionText(), std::string("ab\ncd"));
}

MINI_TEST(selection_word_expansion) {
  Engine e(3, 20);
  feed(e, "foo bar baz");
  e.expandSelectionWord(0, 5);  // double-tap "bar"
  CHECK_EQ(e.selectionText(), std::string("bar"));
}

MINI_TEST(selection_word_expansion_cjk) {
  Engine e(3, 20);
  feed(e, "你好 world");
  e.expandSelectionWord(0, 0);
  CHECK_EQ(e.selectionText(), std::string("你好"));  // CJK = word
}

MINI_TEST(selection_line_expansion) {
  Engine e(4, 10);
  feed(e, "alpha beta\r\ngamma");
  e.expandSelectionLine(0, 3);
  CHECK_EQ(e.selectionText(), std::string("alpha beta"));
}

MINI_TEST(selection_clears) {
  Engine e(3, 20);
  feed(e, "hello");
  e.beginSelection(0, 0);
  e.extendSelection(0, 5);
  e.clearSelection();
  CHECK(e.selectionText().empty());
  auto snap = e.renderSnapshot(0);
  CHECK_EQ(snap.selStartRow, int64_t(-1));
}

MINI_TEST(selection_in_snapshot) {
  Engine e(3, 20);
  feed(e, "hello");
  e.beginSelection(0, 1);
  e.extendSelection(0, 4);
  auto snap = e.renderSnapshot(0);
  CHECK_EQ(snap.selStartRow, int64_t(0));
  CHECK_EQ(snap.selStartCol, 1);
  CHECK_EQ(snap.selEndRow, int64_t(0));
  CHECK_EQ(snap.selEndCol, 4);
}

MINI_TEST(selection_spans_scrollback) {
  Engine e(2, 10, 100);
  feed(e, "aaaa\r\nbbbb\r\ncccc");
  // 3 lines written, screen 2 rows → 1 row in scrollback ("aaaa")
  int64_t base = 0;
  e.beginSelection(base, 0);      // scrollback row 0 (global 0)
  e.extendSelection(base + 2, 2); // screen row 1
  CHECK_EQ(e.selectionText(), std::string("aaaa\nbbbb\ncc"));
}

// ═══ Session persistence ═══

MINI_TEST(session_roundtrip_full_state) {
  Engine e(6, 12, 50);
  feed(e, "\x1b]0;My Title\x07");
  feed(e, "line one\r\n");
  feed(e, "\x1b[1;32mgreen bold\x1b[0m plain\r\n");
  feed(e, "中文宽字\r\n");   // wide chars
  feed(e, "éclair\r\n");   // combining mark (e + U+0301)
  for (int i = 0; i < 10; ++i) feed(e, "filler\r\n");  // push scrollback
  feed(e, "final line");
  feed(e, "\x1b[?1002h\x1b[?1006h");  // mouse button + SGR
  feed(e, "\x1b[?2004h");             // bracketed paste
  feed(e, "\x1b[?1h");                // app cursor keys

  auto blob = e.saveSession(200);
  CHECK(!blob.empty());

  auto restored = Engine::restoreSession(blob.data(), blob.size(), 50);
  CHECK(restored != nullptr);
  CHECK_EQ(restored->rows(), 6);
  CHECK_EQ(restored->cols(), 12);
  CHECK_EQ(std::string(restored->title()), std::string("My Title"));
  CHECK_EQ(restored->renderedText(), e.renderedText());  // screen content identical
  CHECK_EQ(restored->scrollbackTotal(), e.scrollbackTotal());
  CHECK_EQ(restored->cursorRow(), e.cursorRow());
  CHECK_EQ(restored->cursorCol(), e.cursorCol());
  // Mouse / paste modes survive.
  auto snap = restored->renderSnapshot(0);
  CHECK_EQ(snap.mouseMode, int8_t(3));
  CHECK_EQ(snap.mouseEncoding, int8_t(2));
  CHECK(snap.bracketedPaste);
  CHECK(snap.applicationCursor);
  // Scrollback text identical.
  auto a = e.scrollbackText(50);
  auto b = restored->scrollbackText(50);
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) CHECK_EQ(a[i], b[i]);
}

MINI_TEST(session_wrap_flags_survive) {
  Engine e(4, 5, 50);
  feed(e, "abcdefghijkl");  // wraps: "abcde" / "fghij" / "kl"
  auto blob = e.saveSession(50);
  auto restored = Engine::restoreSession(blob.data(), blob.size(), 50);
  CHECK(restored != nullptr);
  // The wrap chain is preserved → search across the seam works after restore.
  CHECK_EQ(restored->search("defg", true, false), 1);
  // And selection joining works without newline at the seam.
  restored->beginSelection(0, 2);
  restored->extendSelection(1, 1);
  CHECK_EQ(restored->selectionText(), std::string("cdef"));
}

MINI_TEST(session_rejects_corruption) {
  Engine e(4, 10);
  feed(e, "content");
  auto blob = e.saveSession(50);
  CHECK(Engine::restoreSession(blob.data(), blob.size(), 50) != nullptr);
  // Truncation
  CHECK(Engine::restoreSession(blob.data(), blob.size() / 2, 50) == nullptr);
  // Byte flip (CRC mismatch)
  std::vector<uint8_t> bad = blob;
  bad[bad.size() / 2] ^= 0xFF;
  CHECK(Engine::restoreSession(bad.data(), bad.size(), 50) == nullptr);
  // Garbage
  const uint8_t junk[] = {1, 2, 3, 4, 5};
  CHECK(Engine::restoreSession(junk, sizeof(junk), 50) == nullptr);
  // Empty
  CHECK(Engine::restoreSession(nullptr, 0, 50) == nullptr);
}

MINI_TEST(session_restored_engine_is_live) {
  Engine e(4, 10);
  feed(e, "count: 1\r\ncount: 2\r\ncount: 3");
  auto blob = e.saveSession(50);
  auto restored = Engine::restoreSession(blob.data(), blob.size(), 50);
  CHECK(restored != nullptr);
  // The restored engine keeps processing input.
  feed(*restored, "\r\ncount: 4");
  CHECK_EQ(restored->search("count", true, false), 4);
  feed(*restored, "X");
  CHECK(restored->renderedText().find("X") != std::string::npos);
}

// ═══ Reflow (resize without content loss) ═══

MINI_TEST(reflow_wide_to_narrow_keeps_content) {
  Engine e(3, 10);
  feed(e, "abcdefghij");  // one full row
  e.resize(3, 6);
  auto ls = splitLines(e.renderedText());
  // 10 chars at width 6 → "abcdef" + "ghij"
  CHECK_EQ(ls[0], std::string("abcdef"));
  CHECK_EQ(ls[1], std::string("ghij"));
}

MINI_TEST(reflow_narrow_to_wide_joins_wrapped) {
  Engine e(2, 5);
  feed(e, "abcdefgh");  // wraps: "abcde" / "fgh"
  e.resize(2, 10);
  CHECK_EQ(splitLines(e.renderedText())[0], std::string("abcdefgh"));
}

MINI_TEST(reflow_wide_char_never_splits) {
  Engine e(2, 4);
  feed(e, "中中中");  // 3 wide chars = 6 cols at width 4 → "中" + "中中"? width 4 → "中"+"中" then wraps
  e.resize(2, 5);
  auto ls = splitLines(e.renderedText());
  // At width 5: "中" (2) + "中" (2) + pad → next row "中"
  std::string joined = ls[0] + ls[1];
  CHECK_EQ(joined, std::string("中中中"));
}

MINI_TEST(reflow_preserves_styles) {
  Engine e(2, 6);
  feed(e, "\x1b[31mred\x1b[0mplain");
  e.resize(2, 4);
  auto snap = e.renderSnapshot(0);
  // "redp" + "lain": first 3 cells red, 4th plain
  CHECK_EQ(snap.visible[0].cells[0].fg, uint32_t(0xFF800000u | 0u));  // basic red = 0x800000
  CHECK_EQ(snap.visible[0].cells[2].fg, uint32_t(0xFF800000u));
  CHECK_EQ(snap.visible[0].cells[3].fg, uint32_t(0));
}

MINI_TEST(reflow_height_shrink_feeds_scrollback) {
  Engine e(6, 10);
  feed(e, "1\r\n2\r\n3\r\n4\r\n5\r\n6");
  e.resize(3, 10);
  CHECK_EQ(e.scrollbackTotal(), 3);  // rows 4..6 pushed to history
  CHECK_EQ(splitLines(e.renderedText())[0], std::string("4"));  // bottom 3 visible
  CHECK_EQ(e.cursorRow(), 2);  // cursor on the last line
  auto sb = e.scrollbackText(10);
  CHECK_EQ(sb.size(), size_t(3));
  CHECK_EQ(sb[0], std::string("1"));
}

MINI_TEST(reflow_scrollback_content_survives_width_change) {
  Engine e(4, 5, 100);
  feed(e, "aaaaa\r\nbbbbb\r\nccccc\r\nddddd");
  e.resize(4, 8);
  // All logical lines re-wrap to width 8: nothing lost, nothing in sb
  CHECK_EQ(e.scrollbackTotal(), 0);
  CHECK_EQ(e.search("aaaaa", true, false), 1);
  CHECK_EQ(e.search("ddddd", true, false), 1);
}

MINI_TEST(reflow_cursor_follows_text) {
  Engine e(3, 5);
  feed(e, "hello");      // cursor at (0,4), wrapPending
  e.resize(3, 10);
  // "hello" now fits one row; cursor should still sit right after "hello"
  CHECK_EQ(e.cursorRow(), 0);
  CHECK_EQ(e.cursorCol(), 5);
}

// ═══ Wheel routing (mouse / alt-scroll) ═══

MINI_TEST(wheel_routes_to_mouse_when_tracking) {
  Engine e(5, 20);
  feed(e, "\x1b[?1000h\x1b[?1006h");
  std::string up = vec(e.encodeWheel(0));
  CHECK_EQ(up, std::string("\x1b[<64;1;1M"));  // SGR wheel-up at (1,1)
}

MINI_TEST(wheel_alt_scroll_arrows) {
  Engine e(5, 20);
  feed(e, "\x1b[?1049h\x1b[?1007h");  // alt screen + alternate scroll
  CHECK_EQ(vec(e.encodeWheel(0)), std::string("\x1b[A"));
  CHECK_EQ(vec(e.encodeWheel(1)), std::string("\x1b[B"));
  feed(e, "\x1b[?1h");  // app cursor keys
  CHECK_EQ(vec(e.encodeWheel(0)), std::string("\x1bOA"));
  // main screen + 1007 → viewport scroll (empty result)
  feed(e, "\x1b[?1049l");
  CHECK(e.encodeWheel(0).empty());
}

// ═══ Paste encoding ═══

MINI_TEST(paste_wraps_with_brackets) {
  Engine e(3, 20);
  feed(e, "\x1b[?2004h");
  std::string out = vec(e.encodePasteUtf8("hi", 2));
  CHECK_EQ(out, std::string("\x1b[200~hi\x1b[201~"));
  feed(e, "\x1b[?2004l");
  out = vec(e.encodePasteUtf8("hi", 2));
  CHECK_EQ(out, std::string("hi"));
}

MINI_TEST(paste_sanitizes_control_sequences) {
  Engine e(3, 20);
  std::string in = "a\x1b[2Jb";  // embedded ESC sequence — ESC is stripped
  std::string out = vec(e.encodePasteUtf8(in.data(), in.size()));
  // The ESC byte is neutralized; the remaining bytes arrive as inert text
  // (readline/bracketed-paste consumers never see a control sequence).
  CHECK_EQ(out, std::string("a[2Jb"));
  CHECK(out.find('\x1b') == std::string::npos);
}

// ═══ Snapshot mirrors input modes ═══

MINI_TEST(snapshot_mirrors_input_modes) {
  Engine e(3, 20);
  feed(e, "\x1b[?1002h\x1b[?1006h\x1b[?1004h\x1b[?1007h\x1b=");
  auto snap = e.renderSnapshot(0);
  CHECK_EQ(snap.mouseMode, int8_t(3));
  CHECK_EQ(snap.mouseEncoding, int8_t(2));
  CHECK(snap.focusReport);
  CHECK(snap.altScroll);
  CHECK(snap.applicationKeypad);
}

// ═══ Robustness: garbage never crashes ═══

MINI_TEST(fuzz_new_sequences_robust) {
  Engine e(6, 10, 50);
  uint32_t seed = 0xFEEDFACE;
  for (int round = 0; round < 3000; ++round) {
    seed = seed * 1664525u + 1013904223u;
    uint8_t buf[16];
    size_t n = 4 + (seed % 13);
    for (size_t i = 0; i < n; ++i) {
      seed = seed * 1664525u + 1013904223u;
      buf[i] = uint8_t(seed >> 24);
    }
    // Bias toward escape-heavy garbage: new v0.2 sequences get exercised.
    if (buf[0] == 0x1B || (round & 7) == 0) buf[0] = 0x1B;
    e.feed(buf, n);
    if (round % 97 == 0) {
      e.search("zz", true, false);
      e.beginSelection(0, 0);
      e.extendSelection(1, 1);
      (void)e.selectionText();
      e.resize(6, 10);
    }
  }
  // Engine still sane afterwards.
  e.reset();
  feed(e, "ok");
  CHECK_EQ(e.search("ok", true, false), 1);
  CHECK(e.cellFootprint() < size_t(1) << 20);
}
