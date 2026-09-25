// Screen-level tests — wide chars, scrollback ring, erase styles, resize.
#include "mini_test.h"

#include "../src/vt_screen.h"
#include "../src/vt_style.h"
#include "../src/vt_width.h"

using namespace apex::vt;

namespace {

StyleTable styles;
Screen makeScreen(int rows, int cols, int maxSb, bool hasSb = true) {
  return Screen(rows, cols, maxSb, hasSb, styles);
}

}  // namespace

MINI_TEST(wide_char_put_and_trail) {
  Screen s = makeScreen(2, 4, 0, false);
  Cell wide{uint32_t(U'中'), styles.defaultId(), kCellWideLead};
  s.put(0, 1, wide);
  CHECK_EQ(s.cell(0, 1).cp, uint32_t(U'中'));
  CHECK(s.cell(0, 1).flags & kCellWideLead);
  CHECK(s.cell(0, 2).flags & kCellWideTrail);
  CHECK_EQ(s.cell(0, 2).cp, uint32_t(0));
}

MINI_TEST(overwrite_wide_trail_blanks_lead) {
  Screen s = makeScreen(2, 4, 0, false);
  Cell wide{uint32_t(U'中'), styles.defaultId(), kCellWideLead};
  s.put(0, 0, wide);
  Cell x{'x', styles.defaultId(), 0};
  s.put(0, 1, x);  // overwrite the trail → lead blanks
  CHECK_EQ(s.cell(0, 0).cp, uint32_t(' '));
  CHECK_EQ(s.cell(0, 0).flags, uint16_t(0));
  CHECK_EQ(s.cell(0, 1).cp, uint32_t('x'));
}

MINI_TEST(erase_row_carries_given_style) {
  Screen s = makeScreen(1, 4, 0, false);
  Style st;
  st.bg.kind = kColorRgb;
  st.bg.r = 0xFF;
  uint16_t id = styles.intern(st);
  for (int c = 0; c < 4; ++c) s.setCell(0, c, Cell{'x', styles.defaultId(), 0});
  s.eraseRow(0, 1, 3, id);
  CHECK_EQ(s.cell(0, 0).cp, uint32_t('x'));
  CHECK_EQ(s.cell(0, 1).cp, uint32_t(' '));
  CHECK_EQ(s.cell(0, 1).style, id);
}

MINI_TEST(scroll_up_captures_to_scrollback) {
  Screen s = makeScreen(2, 4, 10);
  s.setCell(0, 0, Cell{'a', styles.defaultId(), 0});
  s.setCell(1, 0, Cell{'b', styles.defaultId(), 0});
  s.scrollUp(1, 0, 1);
  CHECK_EQ(s.scrollbackCount(), 1);
  CHECK_EQ(s.scrollbackRowText(0), std::string("a"));
  CHECK_EQ(s.cell(0, 0).cp, uint32_t('b'));
  // bottom row blank (DEFAULT style — not any current style)
  CHECK_EQ(s.cell(1, 0).cp, uint32_t(' '));
}

MINI_TEST(scroll_region_does_not_capture_when_top_not_zero) {
  Screen s = makeScreen(4, 4, 10);
  s.scrollUp(1, 1, 2);
  CHECK_EQ(s.scrollbackCount(), 0);
}

MINI_TEST(scrollback_ring_evicts_oldest) {
  Screen s = makeScreen(2, 2, 3);
  for (int i = 0; i < 8; ++i) {
    s.setCell(0, 0, Cell{uint32_t('0' + i), styles.defaultId(), 0});
    s.scrollUp(1, 0, 1);
  }
  CHECK_EQ(s.scrollbackCount(), 3);
  CHECK_EQ(s.scrollbackRowText(0), std::string("5"));
  CHECK_EQ(s.scrollbackRowText(1), std::string("6"));
  CHECK_EQ(s.scrollbackRowText(2), std::string("7"));
  // Monotonic baseline counts ALL lines ever scrolled (incl. evicted).
  CHECK_EQ(s.scrollbackLinesEver(), int64_t(8));
}

MINI_TEST(scroll_back_within_region_restores) {
  Screen s = makeScreen(4, 4, 10);
  s.setCell(0, 0, Cell{'a', styles.defaultId(), 0});
  s.scrollDown(1, 0, 3);
  // 'a' moved down to row 1
  CHECK_EQ(s.cell(1, 0).cp, uint32_t('a'));
  CHECK_EQ(s.cell(0, 0).cp, uint32_t(' '));
  CHECK_EQ(s.scrollbackCount(), 0);
}

MINI_TEST(insert_delete_lines) {
  Screen s = makeScreen(3, 2, 0, false);
  s.setCell(0, 0, Cell{'a', styles.defaultId(), 0});
  s.setCell(1, 0, Cell{'b', styles.defaultId(), 0});
  s.setCell(2, 0, Cell{'c', styles.defaultId(), 0});
  s.insertLines(1, 1, 1, 2);  // insert blank at row 1 within region [1,2]
  CHECK_EQ(s.cell(1, 0).cp, uint32_t(' '));
  CHECK_EQ(s.cell(2, 0).cp, uint32_t('b'));  // shifted down, 'c' pushed out
  s.deleteLines(1, 1, 1, 2);
  CHECK_EQ(s.cell(1, 0).cp, uint32_t('b'));
  CHECK_EQ(s.cell(2, 0).cp, uint32_t(' '));
}

MINI_TEST(insert_delete_chars_shift_marks) {
  Screen s = makeScreen(1, 4, 0, false);
  s.setCell(0, 0, Cell{'e', styles.defaultId(), 0});
  s.putCombining(0, 0, 0x0301);  // é
  s.insertChars(0, 0, 1);        // insert at col 0 → 'e' + mark shift right
  CHECK_EQ(s.cell(0, 1).cp, uint32_t('e'));
  const auto* comb = s.combiningAt(0, 1);
  CHECK(comb != nullptr);
  if (comb) CHECK_EQ(comb->size(), size_t(1));
  s.deleteChars(0, 0, 1);  // delete col 0 → shift back
  CHECK_EQ(s.cell(0, 0).cp, uint32_t('e'));
  comb = s.combiningAt(0, 0);
  CHECK(comb != nullptr);
}

MINI_TEST(resize_keeps_top_left) {
  Screen s = makeScreen(3, 3, 0, false);
  s.setCell(0, 0, Cell{'a', styles.defaultId(), 0});
  s.setCell(1, 1, Cell{'b', styles.defaultId(), 0});
  s.resize(2, 2);
  CHECK_EQ(s.cell(0, 0).cp, uint32_t('a'));
  CHECK_EQ(s.cell(1, 1).cp, uint32_t('b'));
  CHECK_EQ(s.rows(), 2);
  CHECK_EQ(s.cols(), 2);
}

MINI_TEST(row_text_trims_trailing_blanks) {
  Screen s = makeScreen(1, 5, 0, false);
  s.setCell(0, 0, Cell{'a', styles.defaultId(), 0});
  s.setCell(0, 1, Cell{'b', styles.defaultId(), 0});
  CHECK_EQ(s.rowText(0), std::string("ab"));
  // A styled blank still counts as blank for TEXT projection (style-agnostic)
  Style st;
  st.bg.kind = kColorRgb;
  uint16_t id = styles.intern(st);
  s.setCell(0, 2, Cell{' ', id, 0});
  CHECK_EQ(s.rowText(0), std::string("ab"));
}

MINI_TEST(style_table_interning) {
  Style a;
  a.fg.kind = kColorIndexed;
  a.fg.index = 3;
  Style b = a;
  CHECK_EQ(styles.intern(a), styles.intern(b));  // same style → same id
  Style c = a;
  c.attr = kAttrBold;
  CHECK(styles.intern(a) != styles.intern(c));
  CHECK_EQ(styles.get(styles.intern(a)).fg.index, uint8_t(3));
}

MINI_TEST(zero_width_table) {
  // combining acute
  CHECK_EQ(unicodeWidth(0x0301), 0);
  // ZWJ
  CHECK_EQ(unicodeWidth(0x200D), 0);
  // VS16
  CHECK_EQ(unicodeWidth(0xFE0F), 0);
  // CJK
  CHECK_EQ(unicodeWidth(U'中'), 2);
  // emoji 😀
  CHECK_EQ(unicodeWidth(0x1F600), 2);
  // ASCII
  CHECK_EQ(unicodeWidth('a'), 1);
  // C0
  CHECK_EQ(unicodeWidth(0x0A), 0);
  CHECK_EQ(unicodeWidth(0x9B), 0);
}

MINI_TEST(dec_special_graphics_map) {
  CHECK_EQ(decSpecialGraphics('q'), uint32_t(0x2500));  // ─
  CHECK_EQ(decSpecialGraphics('x'), uint32_t(0x2502));  // │
  CHECK_EQ(decSpecialGraphics('`'), uint32_t(0x25C6));  // ◆
  CHECK_EQ(decSpecialGraphics('l'), uint32_t(0x250C));  // ┌
  CHECK_EQ(decSpecialGraphics('k'), uint32_t(0x2510));  // ┐
  CHECK_EQ(decSpecialGraphics('j'), uint32_t(0x2518));  // ┘
  CHECK_EQ(decSpecialGraphics('m'), uint32_t(0x2514));  // └
  CHECK_EQ(decSpecialGraphics('n'), uint32_t(0x253C));  // ┼
  CHECK_EQ(decSpecialGraphics(0), uint32_t(0));         // unmapped
}
