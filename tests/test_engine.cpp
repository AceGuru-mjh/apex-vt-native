// Engine-level parity tests — ported from the Kotlin TerminalCore 2.0 golden
// suite (TerminalCoreTest / T82TerminalCapabilityTest / T85TerminalParityTest).
#include "mini_test.h"

#include "../include/apex/vt/vt_engine.h"

#include <cstring>
#include <string>
#include <vector>

using namespace apex::vt;

namespace {

Engine core(const char* s, int rows = 10, int cols = 20, int maxSb = 1000) {
  Engine e(rows, cols, maxSb);
  e.feed(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
  return e;
}

void feed(Engine& e, const char* s) {
  e.feed(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      out.push_back(text.substr(start));
      return out;
    }
    out.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
}

std::string rowText(const FlatSnapshot& snap, int r) {
  std::string out;
  for (const FlatCell& c : snap.visible[size_t(r)].cells) {
    uint32_t cp = c.cp;
    if (cp < 0x80) {
      out.push_back(char(cp));
    } else if (cp < 0x800) {
      out.push_back(char(0xC0 | (cp >> 6)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(char(0xE0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (cp >> 18)));
      out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

std::string utf8(uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s.push_back(char(cp));
  } else if (cp < 0x800) {
    s.push_back(char(0xC0 | (cp >> 6)));
    s.push_back(char(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    s.push_back(char(0xE0 | (cp >> 12)));
    s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(char(0x80 | (cp & 0x3F)));
  } else {
    s.push_back(char(0xF0 | (cp >> 18)));
    s.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
    s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(char(0x80 | (cp & 0x3F)));
  }
  return s;
}

}  // namespace

// ═══ basics ═══

MINI_TEST(prints_plain_text) {
  Engine e = core("hello");
  CHECK_EQ(e.renderedText().substr(0, 5), std::string("hello"));
  CHECK_EQ(e.cursorCol(), 5);
}

MINI_TEST(cr_returns_cursor_to_col0) {
  Engine e = core("abc\rX");
  CHECK(e.renderedText().substr(0, 3) == "Xbc");
  CHECK_EQ(e.cursorCol(), 1);
}

MINI_TEST(lf_moves_to_next_row) {
  Engine e = core("ab\ncd");
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls.size(), size_t(10));  // all rows joined (Kotlin renderedText)
  CHECK_EQ(ls[0], std::string("ab"));
  CHECK_EQ(ls[1], std::string("  cd"));  // LF keeps the column (CR resets it)
}

MINI_TEST(sgr_color_applied) {
  Engine e = core("\x1B[31mred");
  FlatSnapshot s = e.renderSnapshot(0);
  // first cell fg = indexed 1 (bright red 0xFF0000 with alpha)
  CHECK_EQ(s.visible[0].cells[0].cp, uint32_t('r'));
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFF800000));
}

MINI_TEST(clear_screen_ed2) {
  Engine e = core("garbage everywhere\x1B[2J");
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls.size(), size_t(10));
  for (const auto& l : ls) CHECK(l.empty());  // every row blank
}

MINI_TEST(cursor_movement) {
  Engine e = core("start\x1B[2A\x1B[3B\x1B[4C\x1B[2D");
  // from (0,5): CUU2 (clamp 0), CUD3 → row3, CUF4 → col9, CUB2 → col7
  CHECK_EQ(e.cursorRow(), 3);
  CHECK_EQ(e.cursorCol(), 7);
}

MINI_TEST(alternate_screen_switch_and_restore) {
  Engine e = core("main content");
  feed(e, "\x1B[?1049h");
  CHECK(e.alternateScreen());
  feed(e, "alt content");
  feed(e, "\x1B[?1049l");
  CHECK(!e.alternateScreen());
  CHECK(e.renderedText().find("main content") != std::string::npos);
  CHECK(e.renderedText().find("alt content") == std::string::npos);
}

MINI_TEST(cjk_wide_char_two_cells) {
  Engine e = core("\xE4\xB8\xAD", 5, 10);  // 中
  CHECK_EQ(e.cursorCol(), 2);
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK(s.visible[0].cells[0].flags & kRfWide);
}

MINI_TEST(cjk_at_last_column_wraps) {
  Engine e = core("ab\xE4\xB8\xAD", 5, 3);  // 'a','b',中 (width 2, only 1 col left)
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls[0].substr(0, 2), std::string("ab"));
  // 中 lands on row 1 (wrapped)
  CHECK(ls.size() >= 2);
  CHECK_EQ(ls[1], utf8(U'中'));
}

MINI_TEST(combining_attaches_to_base) {
  // é = e + combining acute
  Engine e = core("e\xCC\x81", 5, 10);
  CHECK_EQ(e.cursorCol(), 1);
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells.size(), size_t(1));
  CHECK_EQ(s.combPool.size(), size_t(1));
  CHECK_EQ(s.combPool[0], uint32_t(0x0301));
}

MINI_TEST(emoji_width_2) {
  Engine e = core("\xF0\x9F\x98\x80", 5, 10);  // 😀
  CHECK_EQ(e.cursorCol(), 2);
}

MINI_TEST(variation_selector_not_independent) {
  Engine e = core("e\xEF\xB8\x8F", 5, 10);  // e + VS16
  CHECK_EQ(e.cursorCol(), 1);
}

MINI_TEST(zwj_not_independent) {
  // family emoji: 👩 ZWJ 👨
  Engine e = core("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA8", 5, 20);
  // 👩 (2) + ZWJ (0) + 👨 (2) = 4 cells
  CHECK_EQ(e.cursorCol(), 4);
}

MINI_TEST(surrogate_replacement) {
  // Raw CESU-8 lone high surrogate ED A0 80 → invalid UTF-8 → U+FFFD
  Engine e = core("\xED\xA0\x80z", 5, 10);
  // Kotlin parity: surrogate → U+FFFD (width 1)
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].cp, uint32_t(0xFFFD));
  CHECK_EQ(s.visible[0].cells[1].cp, uint32_t('z'));
}

MINI_TEST(tab_moves_to_next_multiple_of_8) {
  Engine e = core("\t", 5, 20);
  CHECK_EQ(e.cursorCol(), 8);
}

MINI_TEST(bell_pending_and_drain) {
  Engine e = core("a\x07b\x07");
  CHECK_EQ(e.drainBell(), int64_t(1));  // first drain consumes
  CHECK_EQ(e.drainBell(), int64_t(1));  // no pending → same seq
  feed(e, "\x07");
  CHECK_EQ(e.drainBell(), int64_t(2));  // second audible BEL increments
}

MINI_TEST(mutations_drained_after_feed) {
  Engine e = core("hi");
  auto m = e.drainMutations();
  CHECK(!m.empty());
  CHECK_EQ(e.drainMutations().size(), size_t(0));
  CHECK(m[0].type == MutationType::kCells);
}

MINI_TEST(ris_resets_everything) {
  Engine e = core("\x1B[31mhello\x1B[5;5H");
  feed(e, "\x1B" "c");
  auto ls = lines(e.renderedText());
  for (const auto& l : ls) CHECK(l.empty());
  CHECK_EQ(e.cursorRow(), 0);
  CHECK_EQ(e.cursorCol(), 0);
  auto m = e.drainMutations();
  CHECK(!m.empty());
  CHECK(m.back().type == MutationType::kFull);
}

MINI_TEST(resize_keeps_content) {
  Engine e = core("hello world", 10, 20);
  e.resize(5, 8);
  CHECK_EQ(e.rows(), 5);
  CHECK_EQ(e.cols(), 8);
  CHECK(e.renderedText().find("hello wo") != std::string::npos);
}

MINI_TEST(binary_garbage_does_not_crash) {
  std::string junk;
  for (int i = 0; i < 4096; ++i) junk.push_back(char(uint8_t(i * 31 + 7)));
  Engine e(5, 5, 10);
  e.feed(reinterpret_cast<const uint8_t*>(junk.data()), junk.size());
  e.flush();
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls.size(), size_t(5));  // one line per row, no crash
}

MINI_TEST(unterminated_osc_does_not_hang) {
  Engine e = core("\x1B]52;c;SGVsbG8=");
  CHECK(e.title() == nullptr);  // OSC never terminated → no title
}

MINI_TEST(osc_sets_title) {
  Engine e = core("\x1B]0;Window Title\x07");
  CHECK(e.title() != nullptr);
  CHECK_EQ(std::string(e.title()), std::string("Window Title"));
}

MINI_TEST(c1_osc_9d_sets_title) {
  const uint8_t seq[] = {0x9D, '0', ';', 't', '9', 'd', 0x07};
  Engine e(5, 10);
  e.feed(seq, sizeof(seq));
  CHECK(e.title() != nullptr);
  CHECK_EQ(std::string(e.title()), std::string("t9d"));
}

MINI_TEST(c1_nel_85) {
  const uint8_t seq[] = {'a', 0x85, 'b'};
  Engine e(5, 10);
  e.feed(seq, sizeof(seq));
  CHECK_EQ(e.cursorRow(), 1);
  CHECK_EQ(e.cursorCol(), 1);
}

MINI_TEST(utf8_split_across_feeds) {
  Engine e(5, 10);
  const uint8_t p1[] = {0xE4};          // first byte of 中
  const uint8_t p2[] = {0xB8, 0xAD};    // rest
  e.feed(p1, 1);
  CHECK_EQ(e.cursorCol(), 0);
  e.feed(p2, 2);
  CHECK_EQ(e.cursorCol(), 2);
}

MINI_TEST(flush_emits_replacement) {
  Engine e(5, 10);
  const uint8_t p1[] = {0xF0, 0x9F};  // incomplete 4-byte seq
  e.feed(p1, 2);
  e.flush();
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells.size(), size_t(1));
  CHECK_EQ(s.visible[0].cells[0].cp, uint32_t(0xFFFD));
}

// ═══ SGR forms ═══

MINI_TEST(semicolon_truecolor) {
  Engine e = core("\x1B[38;2;255;0;0mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFFFF0000));
}

MINI_TEST(colon_truecolor) {
  Engine e = core("\x1B[38:2:0:255:0mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFF00FF00));
}

MINI_TEST(colon_truecolor_with_colorspace) {
  // 38:2:<cs>:r:g:b — colorspace ignored
  Engine e = core("\x1B[38:2:0:0:0:255mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFF0000FF));
}

MINI_TEST(colon_256_color) {
  Engine e = core("\x1B[38:5:196mX");
  FlatSnapshot s = e.renderSnapshot(0);
  // index 196: (196-16)=180 → r=180/36=5%6*51=255, g=(180/6)%6=0, b=0 → red
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFFFF0000));
}

MINI_TEST(colon_underline_style_curly) {
  Engine e = core("\x1B[4:3mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK(s.visible[0].cells[0].flags & kRfUnderline);
}

MINI_TEST(256_color_semicolon) {
  Engine e = core("\x1B[38;5;46mX");
  FlatSnapshot s = e.renderSnapshot(0);
  // index 46: i=30 → r=(30/36)%6=0, g=(30/6)%6=5*51=255, b=0 → bright green
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFF00FF00));
}

MINI_TEST(sgr0_resets) {
  Engine e = core("\x1B[31;1mX\x1B[0mY");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFF800000));
  CHECK_EQ(s.visible[0].cells[1].fg, uint32_t(0));
  CHECK_EQ(s.visible[0].cells[1].flags, uint16_t(0));
}

MINI_TEST(bold_dim_flags) {
  Engine e = core("\x1B[1;2mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].flags, uint16_t(kRfBold | kRfDim));
}

MINI_TEST(bright_foreground) {
  Engine e = core("\x1B[91mX");
  FlatSnapshot s = e.renderSnapshot(0);
  // index 9 → BASIC_16[9] = 0xFF0000
  CHECK_EQ(s.visible[0].cells[0].fg, uint32_t(0xFFFF0000));
}

MINI_TEST(bg_color_set) {
  Engine e = core("\x1B[41mX");
  FlatSnapshot s = e.renderSnapshot(0);
  // bg index 1 → 0x800000
  CHECK_EQ(s.visible[0].cells[0].bg, uint32_t(0xFF800000));
}

// ═══ cursor / wrap semantics (T85) ═══

MINI_TEST(sequential_typing_fills_last_column_before_wrapping) {
  Engine e(3, 4);
  feed(e, "ABCDE");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(rowText(s, 0), std::string("ABCD"));
  CHECK_EQ(rowText(s, 1), std::string("E"));
}

MINI_TEST(wide_char_at_second_to_last_col_fills_last_cell) {
  Engine e(3, 4);
  feed(e, "ab\xE4\xB8\xAD""c");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(rowText(s, 0), std::string("ab") + utf8(U'中'));
  CHECK_EQ(rowText(s, 1), std::string("c"));
}

MINI_TEST(cha_clears_pending_wrap) {
  Engine e(2, 4);
  feed(e, "ABCD");
  feed(e, "\x1B[1G");
  feed(e, "Z");
  CHECK_EQ(rowText(e.renderSnapshot(0), 0), std::string("ZBCD"));
}

MINI_TEST(bs_clears_pending_wrap) {
  Engine e(2, 4);
  feed(e, "ABCD");   // wrap pending, cursor col 3
  feed(e, "\x08X");  // BS clears wrapPending, col 3→2; X overwrites C
  CHECK_EQ(rowText(e.renderSnapshot(0), 0), std::string("ABXD"));
}

// ═══ scroll region / origin mode ═══

MINI_TEST(scroll_region_decstbm_scrolls_within) {
  Engine e(6, 10);
  feed(e, "\x1B[2;4r");
  feed(e, "\x1B[2;1H");
  feed(e, "a\nb\nc\nd");
  // 4 lines into a 3-row region → scrolls within region; top line NOT saved
  // to scrollback (top != 0)
  CHECK_EQ(e.scrollbackTotal(), 0);
}

MINI_TEST(scroll_region_lf_saves_to_scrollback_when_top_zero) {
  Engine e(3, 10, 10);
  feed(e, "1\r\n2\r\n3\r\n4");
  CHECK(e.scrollbackTotal() >= 1);
  auto sb = e.scrollbackText(10);
  CHECK_EQ(sb[0], std::string("1"));
}

MINI_TEST(decom_origin_mode_offsets_cup) {
  Engine e(10, 10);
  feed(e, "\x1B[2;5r");
  feed(e, "\x1B[?6h");
  feed(e, "\x1B[1;1H");
  CHECK_EQ(e.cursorRow(), 1);  // region top (2-1) + 0
  CHECK_EQ(e.cursorCol(), 0);
}

MINI_TEST(decom_origin_mode_offsets_vpa) {
  Engine e(10, 10);
  feed(e, "\x1B[3;7r");
  feed(e, "\x1B[?6h");
  feed(e, "\x1B[2d");
  CHECK_EQ(e.cursorRow(), 3);
}

MINI_TEST(csi_0_a_moves_one_row_up) {
  Engine e = core("\x1B[5;5H\x1B[0A");
  CHECK_EQ(e.cursorRow(), 3);
}

MINI_TEST(csi_1_0_r_bottom_means_full_screen) {
  Engine e(6, 10);
  feed(e, "\x1B[1;0r");
  // bottom param 0 → default (rows) → region [0, 5]; cursor home
  feed(e, "\x1B[6;6H");
  feed(e, "x");
  CHECK_EQ(e.renderedText().find("x") != std::string::npos, true);
}

// ═══ edit ops ═══

MINI_TEST(ich_inserts_cells) {
  Engine e = core("ABCDEF", 5, 10);
  feed(e, "\x1B[1;1H\x1B[2@");
  CHECK_EQ(e.renderedText().substr(0, 8), std::string("  ABCDEF"));
}

MINI_TEST(dch_deletes_cells) {
  Engine e = core("ABCDEF", 5, 10);
  feed(e, "\x1B[1;1H\x1B[2P");
  CHECK_EQ(e.renderedText().substr(0, 4), std::string("CDEF"));
}

MINI_TEST(ech_erases_n_chars) {
  Engine e = core("ABCDEF", 5, 10);
  feed(e, "\x1B[1;2H\x1B[2X");
  CHECK_EQ(e.renderedText().substr(0, 6), std::string("A  DEF"));
}

MINI_TEST(irm_insert_mode) {
  Engine e = core("AB", 5, 10);
  feed(e, "\x1B[4h");
  feed(e, "\x1B[1;1H");
  feed(e, "X");
  CHECK_EQ(e.renderedText().substr(0, 3), std::string("XAB"));
}

MINI_TEST(il_dl_lines) {
  Engine e = core("1\r\n2\r\n3\r\n4", 6, 10);
  feed(e, "\x1B[2;1H");
  feed(e, "\x1B[1L");  // insert 1 line at row 2
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls[0], std::string("1"));
  CHECK_EQ(ls[1], std::string(""));
  CHECK_EQ(ls[2], std::string("2"));
  feed(e, "\x1B[1M");  // delete 1 line at row 2
  ls = lines(e.renderedText());
  CHECK_EQ(ls[1], std::string("2"));
}

// ═══ scrollback (T85 M-2 / P-2) ═══

MINI_TEST(scrollback_base_monotonic_and_survives_eviction) {
  Engine e(2, 4, 3);
  for (int i = 1; i <= 8; ++i) {
    std::string line = "l" + std::to_string(i);
    feed(e, line.c_str());
    feed(e, "\r\n");
  }
  FlatSnapshot s = e.renderSnapshot(3);
  // 8 feeds each ending CRLF: first line lands on screen, 7 scroll out
  CHECK_EQ(s.scrollbackBase, int64_t(7));
  CHECK_EQ(s.scrollback.size(), size_t(3));
  CHECK_EQ(s.scrollbackTotal, 3);
}

MINI_TEST(scrollback_rows_preserve_order) {
  Engine e(2, 8, 10);
  feed(e, "one\r\ntwo\r\nthree\r\nfour\r\n");
  FlatSnapshot s = e.renderSnapshot(10);
  std::string t0, t1, t2;
  for (const FlatCell& c : s.scrollback[0].cells) t0.push_back(char(c.cp));
  for (const FlatCell& c : s.scrollback[1].cells) t1.push_back(char(c.cp));
  for (const FlatCell& c : s.scrollback[2].cells) t2.push_back(char(c.cp));
  CHECK_EQ(t0, std::string("one"));
  CHECK_EQ(t1, std::string("two"));
  CHECK_EQ(t2, std::string("three"));
}

MINI_TEST(ed3_clears_scrollback_only) {
  Engine e(4, 10, 100);
  feed(e, "1\r\n2\r\n3\r\n4\r\n5");
  CHECK(e.scrollbackTotal() > 0);
  feed(e, "\x1B[3J");
  CHECK_EQ(e.scrollbackTotal(), 0);
  // visible screen intact
  CHECK(e.renderedText().find("5") != std::string::npos);
}

MINI_TEST(scrollback_text_api) {
  Engine e(2, 10, 100);
  feed(e, "aa\r\nbb\r\n");
  auto sb = e.scrollbackText(10);
  CHECK_EQ(sb.size(), size_t(1));
  CHECK_EQ(sb[0], std::string("aa"));
  auto sb2 = e.scrollbackText(0);
  CHECK_EQ(sb2.size(), size_t(0));
}

// ═══ responses (T85 DA/DSR) ═══

MINI_TEST(da1_responds_vt102) {
  Engine e = core("\x1B[c");
  auto r = e.pollResponses();
  std::string s(r.begin(), r.end());
  CHECK_EQ(s, std::string("\x1B[?6c"));
}

MINI_TEST(da2_responds_secondary) {
  Engine e = core("\x1B[>c");
  auto r = e.pollResponses();
  std::string s(r.begin(), r.end());
  CHECK_EQ(s, std::string("\x1B[>0;276;0c"));
}

MINI_TEST(dsr6_reports_cursor_1_based) {
  Engine e = core("ab\x1B[6n");
  auto r = e.pollResponses();
  std::string s(r.begin(), r.end());
  CHECK_EQ(s, std::string("\x1B[1;3R"));
}

MINI_TEST(dsr5_responds_ok) {
  Engine e = core("\x1B[5n");
  auto r = e.pollResponses();
  std::string s(r.begin(), r.end());
  CHECK_EQ(s, std::string("\x1B[0n"));
}

MINI_TEST(responses_dropped_without_sink) {
  Engine e = core("\x1B[c");
  CHECK(e.pollResponses().size() > 0);
  CHECK_EQ(e.pollResponses().size(), size_t(0));  // drained
}

// ═══ REP / DECSCUSR / DECSTR (T85) ═══

MINI_TEST(rep_repeats_last_printable) {
  Engine e = core("x\x1B[3b", 5, 20);
  CHECK(e.renderedText().find("xxxx") != std::string::npos);
  CHECK_EQ(e.cursorCol(), 4);
}

MINI_TEST(rep_without_prior_printable_noop) {
  Engine e = core("\x1B[5b", 5, 20);
  CHECK_EQ(e.cursorCol(), 0);
}

MINI_TEST(decscusr_changes_cursor_style) {
  Engine e = core("\x1B[4 q");
  CHECK(e.cursorShape() == CursorShape::kUnderline);
  feed(e, "\x1B[6 q");
  CHECK(e.cursorShape() == CursorShape::kBar);
  feed(e, "\x1B[1 q");
  CHECK(e.cursorShape() == CursorShape::kBlock);
}

MINI_TEST(hpa_hpr_vpr) {
  Engine e = core("\x1B[5`", 5, 20);   // HPA 5 → col 4
  CHECK_EQ(e.cursorCol(), 4);
  feed(e, "\x1B[3a");                  // HPR 3 → col 7
  CHECK_EQ(e.cursorCol(), 7);
  feed(e, "\x1B[2e");                  // VPR 2 → row 2
  CHECK_EQ(e.cursorRow(), 2);
}

MINI_TEST(decstr_keeps_content_resets_modes) {
  Engine e = core("\x1B[?1hhello", 5, 20);
  feed(e, "\x1B[!p");
  CHECK(!e.applicationCursor());
  CHECK_EQ(e.cursorRow(), 0);
  CHECK_EQ(e.cursorCol(), 0);
  CHECK(e.renderedText().find("hello") != std::string::npos);
}

MINI_TEST(ris_resets_tab_stops) {
  Engine e(5, 20);
  feed(e, "\x1B[3g");  // clear all tabs
  feed(e, "\t");
  CHECK_EQ(e.cursorCol(), 19);  // no stops → clamps to last col
  feed(e, "\x1B" "c");
  feed(e, "\x1B[1;1H\t");
  CHECK_EQ(e.cursorCol(), 8);  // default 8-col stops restored
}

MINI_TEST(ris_resets_cursor_keys_and_reverse_video) {
  Engine e(5, 20);
  feed(e, "\x1B[?1h\x1B[?5h");
  CHECK(e.applicationCursor());
  CHECK(e.reverseVideo());
  feed(e, "\x1B" "c");
  CHECK(!e.applicationCursor());
  CHECK(!e.reverseVideo());
}

MINI_TEST(decset_1049_saves_and_restores_cursor) {
  Engine e = core("abc", 5, 20);
  feed(e, "\x1B[?1049h");
  CHECK_EQ(e.cursorRow(), 0);
  CHECK_EQ(e.cursorCol(), 0);
  feed(e, "\x1B[3;3Hxyz");
  feed(e, "\x1B[?1049l");
  // cursor restored to (0,3)
  CHECK_EQ(e.cursorRow(), 0);
  CHECK_EQ(e.cursorCol(), 3);
}

MINI_TEST(mode_2004_bracketed_paste) {
  Engine e = core("\x1B[?2004h");
  CHECK(e.bracketedPaste());
  feed(e, "\x1B[?2004l");
  CHECK(!e.bracketedPaste());
}

MINI_TEST(decsc_decrc_restore) {
  Engine e = core("\x1B[2;3H\x1B" "7");  // CUP + DECSC
  feed(e, "\x1B[1;1H");
  feed(e, "\x1B" "8");  // DECRC
  CHECK_EQ(e.cursorRow(), 1);  // 0-based row 2-1
  CHECK_EQ(e.cursorCol(), 2);
}

MINI_TEST(ansi_sys_save_restore) {
  Engine e = core("\x1B[2;3H\x1B[s");
  feed(e, "\x1B[1;1H\x1B[u");
  CHECK_EQ(e.cursorRow(), 1);
  CHECK_EQ(e.cursorCol(), 2);
}

MINI_TEST(ri_reverse_index_scrolls_down_at_top) {
  Engine e = core("1\r\n2\r\n3", 4, 10);
  feed(e, "\x1B[1;1H");  // cursor to row 0
  feed(e, "\x1BM");     // RI at top → scroll down
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls[0], std::string(""));
  CHECK_EQ(ls[1], std::string("1"));
}

MINI_TEST(ind_nel_at_bottom_scroll) {
  Engine e = core("1\r\n2", 2, 10);
  feed(e, "\x1B[2;1H");
  feed(e, "\x1B" "D");  // IND at bottom → scroll up
  CHECK(e.scrollbackTotal() >= 1);
  feed(e, "\x1B" "E");  // NEL
  CHECK_EQ(e.cursorRow(), 1);
  CHECK_EQ(e.cursorCol(), 0);
}

MINI_TEST(hts_sets_tab_stop) {
  Engine e(5, 20);
  feed(e, "\x1B[3G\x1B" "H");  // move to col 2, set stop
  feed(e, "\x1B[1G\t");     // tab from col 0 → stops at col 2
  CHECK_EQ(e.cursorCol(), 2);
}

// ═══ DEC Special Graphics (T82) ═══

MINI_TEST(dec_graphics_charset_renders_box_drawing) {
  Engine e = core("\x1B(0lqk", 5, 20);  // ┌─┐
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[0].cp, uint32_t(0x250C));
  CHECK_EQ(s.visible[0].cells[1].cp, uint32_t(0x2500));
  CHECK_EQ(s.visible[0].cells[2].cp, uint32_t(0x2510));
}

MINI_TEST(dec_graphics_reset_by_ascii_designation) {
  Engine e = core("\x1B(0lqk\x1B(Bx", 5, 20);
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK_EQ(s.visible[0].cells[3].cp, uint32_t('x'));
}

// ═══ OSC 52 clipboard ═══

MINI_TEST(osc52_clipboard_request) {
  Engine e = core("\x1B]52;c;SGVsbG8sIFdvcmxkIQ==\x07");  // "Hello, World!"
  auto reqs = e.drainClipboardRequests();
  CHECK_EQ(reqs.size(), size_t(1));
  CHECK_EQ(reqs[0], std::string("Hello, World!"));
}

MINI_TEST(osc52_invalid_base64_ignored) {
  Engine e = core("\x1B]52;c;!!!not-base64!!!\x07");
  CHECK_EQ(e.drainClipboardRequests().size(), size_t(0));
}

MINI_TEST(osc52_empty_payload_is_query_ignored) {
  Engine e = core("\x1B]52;c;\x07");
  CHECK_EQ(e.drainClipboardRequests().size(), size_t(0));
}

// ═══ mutation semantics ═══

MINI_TEST(erase_mutation_type) {
  Engine e = core("x\x1B[2J");
  auto m = e.drainMutations();
  bool hasErase = false;
  for (const auto& mm : m) hasErase |= mm.type == MutationType::kErase;
  CHECK(hasErase);
}

MINI_TEST(il_dl_mutation_types) {
  Engine e = core("x\x1B[1L");
  auto m = e.drainMutations();
  bool hasIl = false;
  for (const auto& mm : m) hasIl |= mm.type == MutationType::kInsertLines;
  CHECK(hasIl);
  feed(e, "\x1B[1M");
  m = e.drainMutations();
  bool hasDl = false;
  for (const auto& mm : m) hasDl |= mm.type == MutationType::kDeleteLines;
  CHECK(hasDl);
}

MINI_TEST(resize_mutation_type) {
  Engine e(5, 10);
  e.resize(6, 12);
  auto m = e.drainMutations();
  CHECK(!m.empty());
  CHECK(m.back().type == MutationType::kResize);
}

MINI_TEST(full_mutation_on_alt_screen_switch) {
  Engine e = core("x\x1B[?1049h");
  auto m = e.drainMutations();
  bool hasFull = false;
  for (const auto& mm : m) hasFull |= mm.type == MutationType::kFull;
  CHECK(hasFull);
}

MINI_TEST(c0_always_emits_cursor_row_mutation) {
  Engine e = core("a");
  e.drainMutations();
  const uint8_t nul[] = {0x00};
  e.feed(nul, 1);  // ignored C0 still mutates cursor row (Kotlin parity)
  CHECK_EQ(e.drainMutations().size(), size_t(1));
}

// ═══ inverted video resolution ═══

MINI_TEST(sgr7_inverse_flag) {
  Engine e = core("\x1B[7mX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK(s.visible[0].cells[0].flags & kRfInverse);
}

MINI_TEST(global_reverse_video_flags_all_cells) {
  Engine e = core("\x1B[?5hX");
  FlatSnapshot s = e.renderSnapshot(0);
  CHECK(s.visible[0].cells[0].flags & kRfInverse);
}

MINI_TEST(erase_bg_persists_in_projection) {
  // ED with a bg color set → erased cells carry the style (non-default blank
  // → NOT trimmed → row renders with the styled blank cells)
  Engine e = core("\x1B[41mX\x1B[1;2H\x1B[0K", 2, 4);
  FlatSnapshot s = e.renderSnapshot(0);
  // Cell 0 'X' has red bg; erased col 1..3 also red bg → 4 cells present
  CHECK_EQ(s.visible[0].cells.size(), size_t(4));
  CHECK_EQ(s.visible[0].cells[3].bg, uint32_t(0xFF800000));
}

// ═══ misc hardening ═══

MINI_TEST(lnm_newline_mode) {
  Engine e = core("\x1B[20hab\ncd", 5, 10);
  // LNM: LF also CRs
  auto ls = lines(e.renderedText());
  CHECK_EQ(ls[1], std::string("cd"));
  CHECK_EQ(e.cursorCol(), 2);
}

MINI_TEST(footprint_bounded) {
  Engine e(24, 80, 1000);
  std::string chunk(1024, 'x');
  size_t before = e.cellFootprint();
  for (int i = 0; i < 2000; ++i) {
    e.feed(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
    if (i % 500 == 0) e.drainMutations();
  }
  size_t after = e.cellFootprint();
  // 2000 lines scrolled → scrollback ring capped at 1000 rows.
  CHECK(after <= before + 1000 * 80 * 8 + 8 * 80);
  CHECK_EQ(e.pendingMutationCount() <= 4097, true);
}

MINI_TEST(zero_scrollback_engine) {
  Engine e(2, 4, 0);
  feed(e, "1\r\n2\r\n3\r\n4");
  CHECK_EQ(e.scrollbackTotal(), 0);
  CHECK_EQ(e.renderedText().find("4") != std::string::npos, true);
}
