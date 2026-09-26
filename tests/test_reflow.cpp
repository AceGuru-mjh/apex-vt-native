// Reflow tests — content preservation across width changes, wide-char row
// protection, combining-mark following, cursor mapping, style pass-through,
// and the documented bounds (see src/vt_reflow.h).
#include "mini_test.h"

#include "../src/vt_lines.h"
#include "../src/vt_reflow.h"

#include <string>
#include <vector>

using namespace apex::vt;
using namespace apex::vt::lines;

namespace {

struct VisualRow {
  std::vector<Cell> cells;
  bool wrapped = false;
  RowRef ref() const { return RowRef{cells.data(), int(cells.size()), wrapped, nullptr}; }
};

VisualRow rowOf(const char* text, bool wrapped, uint16_t style = 0) {
  VisualRow r;
  r.wrapped = wrapped;
  for (const char* p = text; *p != '\0'; ++p) {
    r.cells.push_back(Cell{uint32_t(*p), style, 0});
  }
  return r;
}

// Row with one wide char at [wideCol] (lead + trail cells). Text characters
// are placed around it; a wideCol equal to the text length appends at the end.
VisualRow wideRow(const char* text, uint32_t wideCp, int wideCol, uint16_t style,
                  bool wrapped) {
  VisualRow r;
  r.wrapped = wrapped;
  for (const char* p = text; *p != '\0'; ++p) {
    if (int(r.cells.size()) == wideCol) {
      r.cells.push_back(Cell{wideCp, style, kCellWideLead});
      r.cells.push_back(Cell{uint32_t(0), style, kCellWideTrail});
    }
    r.cells.push_back(Cell{uint32_t(*p), style, 0});
  }
  if (int(r.cells.size()) == wideCol) {
    r.cells.push_back(Cell{wideCp, style, kCellWideLead});
    r.cells.push_back(Cell{uint32_t(0), style, kCellWideTrail});
  }
  return r;
}

VisualRow blankRow(int count, uint16_t style) {
  VisualRow r;
  r.wrapped = false;
  r.cells.assign(size_t(count), Cell{uint32_t(' '), style, 0});
  return r;
}

// Row from explicit cells (mixed styles / wide pairs by hand).
VisualRow cellsRow(std::vector<Cell> cells, bool wrapped) {
  VisualRow r;
  r.wrapped = wrapped;
  r.cells = std::move(cells);
  return r;
}

// rows must outlive the ReflowInput (RowRefs borrow the cells).
void fillInput(ReflowInput* in, const std::vector<VisualRow>& rows, int newCols,
               int cursorRow, int cursorCol) {
  std::vector<RowRef> refs;
  refs.reserve(rows.size());
  for (const VisualRow& r : rows) refs.push_back(r.ref());
  lines::assemble(refs.data(), int(refs.size()), 0, 2000, &in->lines);
  in->newCols = newCols;
  in->cursorRow = cursorRow;
  in->cursorCol = cursorCol;
}

// Screen::rowText-style projection of one output row.
std::string textOf(const std::vector<Cell>& row) {
  std::string out;
  for (const Cell& cell : row) {
    if (cell.flags & kCellWideTrail) continue;
    const uint32_t cp = cell.cp == 0 ? uint32_t(' ') : cell.cp;
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
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace

MINI_TEST(reflow_narrow_to_wide_preserves_content) {
  // "ab cd" at 2 columns (ab / ' c' / d) → 4 columns: "ab c" + "d".
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf(" c", true), rowOf("d", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(int(out.rows.size()), 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("ab c"));
  CHECK_EQ(textOf(out.rows[1]), std::string("d"));
  CHECK_EQ(int(out.rows[0].size()), 4);  // every row exactly newCols wide
  CHECK_EQ(int(out.rows[1].size()), 4);
  CHECK_EQ(int(out.lineOfRow.size()), 2);
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
  CHECK_EQ(out.lineOfRow[1], int32_t(0));
}

MINI_TEST(reflow_wide_to_narrow_preserves_content) {
  std::vector<VisualRow> rows = {rowOf("abcd", false)};
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("ab"));
  CHECK_EQ(textOf(out.rows[1]), std::string("cd"));
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
  CHECK_EQ(out.lineOfRow[1], int32_t(0));
}

MINI_TEST(reflow_wide_char_never_straddles_rows) {
  // 4 columns: [a][b][中 lead][中 trail]; reflow to 3 columns → the wide
  // char moves down and the orphan column is padded with ITS style (9).
  std::vector<VisualRow> rows = {wideRow("ab", uint32_t(U'中'), 2, 9, false)};
  CHECK_EQ(int(rows[0].cells.size()), 4);
  ReflowInput in;
  fillInput(&in, rows, 3, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("ab"));
  CHECK_EQ(textOf(out.rows[1]), std::string("\xE4\xB8\xAD"));
  // Row 0: a, b, pad — pad carries the wide char's style (xterm bg inherit).
  CHECK_EQ(out.rows[0][0].cp, uint32_t('a'));
  CHECK_EQ(out.rows[0][1].cp, uint32_t('b'));
  CHECK_EQ(out.rows[0][2].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][2].style, uint16_t(9));
  CHECK_EQ(out.rows[0][2].flags, uint16_t(0));
  // Row 1: regenerated lead + trail carry the wide char's style.
  CHECK(out.rows[1][0].flags & kCellWideLead);
  CHECK_EQ(out.rows[1][0].cp, uint32_t(U'中'));
  CHECK(out.rows[1][1].flags & kCellWideTrail);
  CHECK_EQ(out.rows[1][1].style, uint16_t(9));
  CHECK_EQ(int(out.rows[1].size()), 3);
}

MINI_TEST(reflow_combining_marks_follow_base_cells) {
  // "ab" + "cd" → one row "abcd"; marks on b (row 0, col 1) and c (row 1,
  // col 0) rewrite to their bases' new positions (cols 1 and 2).
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  in.combs.push_back(CombRun{1, 0, {0x301}});  // on 'c'
  in.combs.push_back(CombRun{0, 1, {0x308}});  // on 'b'
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(int(out.rowCombs.size()), 1);
  CHECK_EQ(int(out.rowCombs[0].size()), 2);
  // Emission order is col order within the row.
  CHECK_EQ(out.rowCombs[0][0].row, int32_t(0));
  CHECK_EQ(out.rowCombs[0][0].col, int32_t(1));
  CHECK(out.rowCombs[0][0].cps == std::vector<uint32_t>{0x308});
  CHECK_EQ(out.rowCombs[0][1].row, int32_t(0));
  CHECK_EQ(out.rowCombs[0][1].col, int32_t(2));
  CHECK(out.rowCombs[0][1].cps == std::vector<uint32_t>{0x301});
}

MINI_TEST(reflow_comb_global_rows_across_lines) {
  // Two logical lines: L0 owns input rows 0-1, L1 owns row 2 ('e' there).
  std::vector<VisualRow> rows = {
      rowOf("ab", true), rowOf("cd", false), rowOf("ef", false),
  };
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  in.combs.push_back(CombRun{2, 0, {0x30A}});  // on 'e' — global row 2
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("abcd"));
  CHECK_EQ(textOf(out.rows[1]), std::string("ef"));
  CHECK_EQ(int(out.rowCombs[1].size()), 1);
  CHECK_EQ(out.rowCombs[1][0].row, int32_t(1));
  CHECK_EQ(out.rowCombs[1][0].col, int32_t(0));
  CHECK(out.rowCombs[0].empty());
}

MINI_TEST(reflow_cursor_maps_to_same_text) {
  // "ab"+"cd" → 4 cols "abcd": cursor on 'd' (input row 1, col 1).
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 1, 1);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 3);  // 'd' now at (0, 3)
  // Cursor past the last cell (col == count) → end of line.
  fillInput(&in, rows, 4, 1, 2);
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 4);
}

MINI_TEST(reflow_cursor_on_wide_lead_and_trail) {
  // [x][中 lead][trail] at 3 columns; reflow to 5.
  std::vector<VisualRow> rows = {wideRow("x", uint32_t(U'中'), 1, 0, false)};
  CHECK_EQ(int(rows[0].cells.size()), 3);
  ReflowInput in;
  fillInput(&in, rows, 5, 0, 1);  // cursor ON the wide lead
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 1);  // still the lead column
  fillInput(&in, rows, 5, 0, 2);  // cursor ON the trail → after the char
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 3);  // end of line (after the wide cell)
}

MINI_TEST(reflow_cursor_outside_lines_parks_at_origin) {
  std::vector<VisualRow> rows = {rowOf("ab", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 99, 0);
  in.cursorFirstAbsRow = 123;  // cannot be projected → (0, 0)
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 0);
  fillInput(&in, rows, 4, -1, 0);
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 0);
}

MINI_TEST(reflow_style_ids_pass_through) {
  std::vector<VisualRow> rows = {rowOf("abc", false, 7)};
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rows[0][0].style, uint16_t(7));
  CHECK_EQ(out.rows[0][1].style, uint16_t(7));
  CHECK_EQ(out.rows[1][0].style, uint16_t(7));
}

MINI_TEST(reflow_lines_never_share_a_row) {
  // L0 fills row 0 exactly ("abcd"); L1 must start a fresh row.
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false), rowOf("ef", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("abcd"));
  CHECK_EQ(textOf(out.rows[1]), std::string("ef"));
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
  CHECK_EQ(out.lineOfRow[1], int32_t(1));
}

MINI_TEST(reflow_newcols_clamped) {
  std::vector<VisualRow> rows = {rowOf("ab", false)};
  ReflowInput in;
  fillInput(&in, rows, 0, 0, 0);  // → 1
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(int(out.rows[0].size()), 1);
  CHECK_EQ(textOf(out.rows[0]), std::string("a"));
  fillInput(&in, rows, 1000, 0, 0);  // → 512
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(int(out.rows[0].size()), 512);
}

MINI_TEST(reflow_newcols_one_degrades_wide_to_narrow) {
  // A wide char can never fit one column: keep the code point, drop the
  // wide flags — bounded and total instead of looping.
  std::vector<VisualRow> rows = {wideRow("", uint32_t(U'中'), 0, 4, false)};
  CHECK_EQ(int(rows[0].cells.size()), 2);
  ReflowInput in;
  fillInput(&in, rows, 1, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(int(out.rows[0].size()), 1);
  CHECK_EQ(out.rows[0][0].cp, uint32_t(U'中'));
  CHECK_EQ(out.rows[0][0].flags & kCellWideLead, uint16_t(0));
  CHECK_EQ(out.rows[0][0].style, uint16_t(4));
}

MINI_TEST(reflow_all_blank_line_keeps_one_styled_row) {
  // 4 blank cells with bg style 3 → exactly ONE output row, all style 3.
  std::vector<VisualRow> rows = {blankRow(4, 3)};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);  // NOT ceil(4/4)+1 — padding is trimmed
  CHECK_EQ(int(out.rows[0].size()), 4);
  for (int c = 0; c < 4; ++c) {
    CHECK_EQ(out.rows[0][size_t(c)].cp, uint32_t(' '));
    CHECK_EQ(out.rows[0][size_t(c)].style, uint16_t(3));
  }
}

MINI_TEST(reflow_long_blank_tail_does_not_inflate_rows) {
  // "hi" + 78 padding blanks at 80 cols → 40 cols: ONE row.
  VisualRow r = rowOf("hi", false);
  r.cells.resize(80, Cell{uint32_t(' '), 0, 0});
  std::vector<VisualRow> rows = {r};
  ReflowInput in;
  fillInput(&in, rows, 40, 0, 2);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(textOf(out.rows[0]), std::string("hi"));
}

MINI_TEST(reflow_comb_on_trimmed_blank_is_dropped) {
  VisualRow r = rowOf("a", false);
  r.cells.push_back(Cell{uint32_t(' '), 0, 0});  // trailing padding blank
  std::vector<VisualRow> rows = {r};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  in.combs.push_back(CombRun{0, 1, {0x301}});  // anchored on the trimmed cell
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(textOf(out.rows[0]), std::string("a"));
  CHECK(out.rowCombs[0].empty());
}

MINI_TEST(reflow_row_limit_truncates_keeping_head) {
  // 65540 cells at width 1 → capped at 65535 rows (head kept).
  VisualRow r;
  r.wrapped = false;
  r.cells.assign(65540, Cell{uint32_t('a'), 0, 0});
  std::vector<VisualRow> rows = {r};
  ReflowInput in;
  fillInput(&in, rows, 1, 0, 65539);  // cursor beyond the cut
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 65535);
  CHECK_EQ(int(out.rows.size()), 65535);
  CHECK_EQ(int(out.lineOfRow.size()), 65535);
  CHECK_EQ(out.lineOfRow[65534], int32_t(0));
  CHECK_EQ(out.rows[65534][0].cp, uint32_t('a'));
  // Cursor past the cut → END of the kept content.
  CHECK_EQ(out.cursorRow, 65534);
  CHECK_EQ(out.cursorCol, 1);
}

MINI_TEST(reflow_empty_input_yields_empty_output) {
  ReflowInput in;  // no lines
  in.newCols = 80;
  in.cursorRow = 5;
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 0);
  CHECK(out.rows.empty());
  CHECK(out.rowCombs.empty());
  CHECK(out.lineOfRow.empty());
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 0);
}

MINI_TEST(reflow_degenerate_lines) {
  ReflowInput in;
  in.newCols = 4;
  in.lines.push_back(lines::LogicalLine{});  // zero rows → nothing
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 0);
  // A line whose only row has zero cells still renders one blank row.
  lines::RowRef emptyRef{nullptr, 0, false, nullptr};
  lines::LogicalLine one;
  one.rows.push_back(emptyRef);
  in.lines.push_back(one);
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(int(out.rows[0].size()), 4);
  CHECK_EQ(out.rows[0][0].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][0].style, uint16_t(0));
}

MINI_TEST(reflow_rowCombs_parallel_to_rows) {
  std::vector<VisualRow> rows = {rowOf("ab", false), rowOf("cd", false)};
  ReflowInput in;
  fillInput(&in, rows, 8, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(int(out.rowCombs.size()), int(out.rows.size()));
  CHECK_EQ(int(out.lineOfRow.size()), int(out.rows.size()));
  CHECK(out.rowCombs[0].empty());
  CHECK(out.rowCombs[1].empty());
}

MINI_TEST(reflow_exact_fit_emits_no_extra_row) {
  // "abcd" at EXACTLY 4 columns: one row, no trailing empty row, no padding.
  std::vector<VisualRow> rows = {rowOf("abcd", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(textOf(out.rows[0]), std::string("abcd"));
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
}

MINI_TEST(reflow_same_width_keeps_layout) {
  // Rewrap at the unchanged width: same row split, same content per row.
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false)};
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("ab"));
  CHECK_EQ(textOf(out.rows[1]), std::string("cd"));
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
  CHECK_EQ(out.lineOfRow[1], int32_t(0));
}

MINI_TEST(reflow_comb_on_wide_lead_follows) {
  // A mark on a wide LEAD column re-attaches to the wide lead's new spot:
  // [x][中 lead][trail] at 3 cols → 5 cols puts the lead at col 1.
  std::vector<VisualRow> rows = {wideRow("x", uint32_t(U'中'), 1, 5, false)};
  ReflowInput in;
  fillInput(&in, rows, 5, 0, 0);
  in.combs.push_back(CombRun{0, 1, {0x301}});  // anchored on the lead col
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(textOf(out.rows[0]), std::string("x\xE4\xB8\xAD"));
  CHECK(out.rows[0][1].flags & kCellWideLead);  // lead landed at col 1
  CHECK_EQ(int(out.rowCombs[0].size()), 1);
  CHECK_EQ(out.rowCombs[0][0].row, int32_t(0));
  CHECK_EQ(out.rowCombs[0][0].col, int32_t(1));  // follows the lead
  CHECK(out.rowCombs[0][0].cps == std::vector<uint32_t>{0x301});
}

MINI_TEST(reflow_cursor_second_row_first_cell_and_origin) {
  // "ab"+"cd" → 4 cols: cursor on 'c' (input row 1, col 0) → (0, 2).
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false)};
  ReflowInput in;
  fillInput(&in, rows, 4, 1, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 2);
  // Cursor on the very first cell stays at the origin.
  fillInput(&in, rows, 4, 0, 0);
  out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 0);
  CHECK_EQ(out.cursorCol, 0);
}

MINI_TEST(reflow_cursor_past_nonfinal_row_end) {
  // Cursor col beyond a row's cell count clamps to that row's end: ordinal
  // counts ALL of row 0's base cells → end of the logical line.
  std::vector<VisualRow> rows = {rowOf("abcd", false)};
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 99);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.cursorRow, 1);
  CHECK_EQ(out.cursorCol, 2);  // after 'd' (row 1 fills exactly 2 cols)
}

MINI_TEST(reflow_row_end_padding_inherits_last_style) {
  // "abc" (style 6) at 5 columns: the two pad cells inherit style 6
  // (xterm-style bg persistence on row-end padding).
  std::vector<VisualRow> rows = {rowOf("abc", false, 6)};
  ReflowInput in;
  fillInput(&in, rows, 5, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 1);
  CHECK_EQ(int(out.rows[0].size()), 5);
  CHECK_EQ(out.rows[0][2].cp, uint32_t('c'));
  CHECK_EQ(out.rows[0][3].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][3].style, uint16_t(6));
  CHECK_EQ(out.rows[0][4].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][4].style, uint16_t(6));
}

MINI_TEST(reflow_wide_to_narrow_moves_wide_down) {
  // [a(0)][中 lead(9)][trail(9)][b(0)] at 4 cols → 2 cols: a / 中 / b on
  // three rows — built cell by cell so the styles differ per cell.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(U'中'), 9, kCellWideLead},
      Cell{uint32_t(0), 9, kCellWideTrail},
      Cell{uint32_t('b'), 0, 0},
  }, false)};
  CHECK_EQ(int(rows[0].cells.size()), 4);  // a 中lead 中trail b
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 3);
  CHECK_EQ(textOf(out.rows[0]), std::string("a"));
  CHECK_EQ(textOf(out.rows[1]), std::string("\xE4\xB8\xAD"));
  CHECK_EQ(textOf(out.rows[2]), std::string("b"));
  // Row 0's orphan column pad carries the wide char's style (9).
  CHECK_EQ(out.rows[0][1].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][1].style, uint16_t(9));
  CHECK_EQ(out.rows[0][1].flags, uint16_t(0));
  // Row 1 keeps the regenerated lead + trail pair.
  CHECK(out.rows[1][0].flags & kCellWideLead);
  CHECK(out.rows[1][1].flags & kCellWideTrail);
  CHECK_EQ(out.rows[1][1].style, uint16_t(9));
  // Row 2's pad inherits 'b's default style 0.
  CHECK_EQ(out.rows[2][1].cp, uint32_t(' '));
  CHECK_EQ(out.rows[2][1].style, uint16_t(0));
  CHECK_EQ(out.lineOfRow[0], int32_t(0));
  CHECK_EQ(out.lineOfRow[1], int32_t(0));
  CHECK_EQ(out.lineOfRow[2], int32_t(0));
}

MINI_TEST(reflow_two_wide_chars_at_boundary) {
  // [中 lead][trail][中 lead][trail] at 4 → 3: the second wide char does not
  // fit the remaining 1 column → pad + move down. No straddle, no loss.
  std::vector<VisualRow> rows = {wideRow("", uint32_t(U'中'), 0, 9, false)};
  VisualRow second = wideRow("", uint32_t(U'文'), 0, 9, false);
  for (Cell& c : second.cells) rows[0].cells.push_back(c);  // append 文 pair
  CHECK_EQ(int(rows[0].cells.size()), 4);
  ReflowInput in;
  fillInput(&in, rows, 3, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 2);
  CHECK_EQ(textOf(out.rows[0]), std::string("\xE4\xB8\xAD"));
  CHECK_EQ(textOf(out.rows[1]), std::string("\xE6\x96\x87"));
  CHECK_EQ(out.rows[0][2].cp, uint32_t(' '));
  CHECK_EQ(out.rows[0][2].style, uint16_t(9));  // pad inherits 文's style
  CHECK_EQ(int(out.rows[1].size()), 3);
  CHECK_EQ(out.rows[1][2].cp, uint32_t(' '));
}

MINI_TEST(refrow_lineOfRow_multi_line_boundaries) {
  // Three logical lines at width 2: L0 "ab"+"cd", L1 "efgh", L2 "ij"+"kl".
  std::vector<VisualRow> rows = {
      rowOf("ab", true), rowOf("cd", false),
      rowOf("efgh", false),
      rowOf("ij", true), rowOf("kl", false),
  };
  ReflowInput in;
  fillInput(&in, rows, 2, 0, 0);
  ReflowOutput out = ReflowEngine::rewrap(in);
  CHECK_EQ(out.rowsTotal, 6);
  CHECK_EQ(textOf(out.rows[0]), std::string("ab"));
  CHECK_EQ(textOf(out.rows[1]), std::string("cd"));
  CHECK_EQ(textOf(out.rows[2]), std::string("ef"));
  CHECK_EQ(textOf(out.rows[3]), std::string("gh"));
  CHECK_EQ(textOf(out.rows[4]), std::string("ij"));
  CHECK_EQ(textOf(out.rows[5]), std::string("kl"));
  const int32_t expected[6] = {0, 0, 1, 1, 2, 2};
  for (int i = 0; i < 6; ++i) {
    CHECK_EQ(out.lineOfRow[size_t(i)], expected[i]);
  }
}

MINI_TEST(reflow_roundtrip_no_text_loss) {
  // "abcdefghij" folded at 3 cols → rewrap 4 → rewrap back to 3: the
  // concatenated text of every row must reproduce the original stream.
  const std::string original = "abcdefghij";
  std::vector<VisualRow> rows = {
      rowOf("abc", true), rowOf("def", true), rowOf("ghij", false),
  };
  ReflowInput in;
  fillInput(&in, rows, 4, 0, 0);
  ReflowOutput mid = ReflowEngine::rewrap(in);
  std::string joined;
  for (const std::vector<Cell>& r : mid.rows) joined += textOf(r);
  CHECK_EQ(joined, original);

  // Chain mid's rows back into a logical line (rows of one lineOfRow value
  // are contiguous), then rewrap to the original width.
  std::vector<VisualRow> midRows;
  for (size_t i = 0; i < mid.rows.size(); ++i) {
    VisualRow r;
    const bool nextSameLine =
        i + 1 < mid.rows.size() && mid.lineOfRow[i + 1] == mid.lineOfRow[i];
    r.wrapped = nextSameLine;
    r.cells = mid.rows[i];
    midRows.push_back(std::move(r));
  }
  fillInput(&in, midRows, 3, 0, 0);
  ReflowOutput back = ReflowEngine::rewrap(in);
  std::string rejoined;
  for (const std::vector<Cell>& r : back.rows) rejoined += textOf(r);
  CHECK_EQ(rejoined, original);
}
