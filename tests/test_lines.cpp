// Logical-line assembly tests — wrapped chains, truncation bounds, and the
// extractText text/map contract (see src/vt_lines.h).
#include "mini_test.h"

#include "../src/vt_lines.h"
#include "../src/vt_screen.h"
#include "../src/vt_style.h"

using namespace apex::vt;
using namespace apex::vt::lines;

namespace {

// One synthetic visual row. The vector<Cell> owns the storage; RowRef only
// borrows it (mirrors how Screen::rowCells is consumed).
struct VisualRow {
  std::vector<Cell> cells;
  bool wrapped = false;
  RowRef ref() const { return RowRef{cells.data(), int(cells.size()), wrapped, nullptr}; }
};

VisualRow rowOf(const char* text, bool wrapped) {
  VisualRow r;
  r.wrapped = wrapped;
  for (const char* p = text; *p != '\0'; ++p) {
    r.cells.push_back(Cell{uint32_t(*p), 0, 0});
  }
  return r;
}

// Row from explicit cells (wide chars / blanks / multibyte).
VisualRow cellsRow(std::vector<Cell> cells, bool wrapped) {
  VisualRow r;
  r.cells = std::move(cells);
  r.wrapped = wrapped;
  return r;
}

std::vector<RowRef> refsOf(const std::vector<VisualRow>& rows) {
  std::vector<RowRef> refs;
  refs.reserve(rows.size());
  for (const VisualRow& r : rows) refs.push_back(r.ref());
  return refs;
}

std::vector<LogicalLine> assembleAll(const std::vector<VisualRow>& rows,
                                     int maxRowsPerLine = 2000) {
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  assemble(refs.data(), int(refs.size()), 0, maxRowsPerLine, &lines);
  return lines;
}

// (rowInLine << 16) | col — same packing as the map contract.
uint32_t pack(int row, int col) { return (uint32_t(row) << 16) | uint32_t(col); }

}  // namespace

MINI_TEST(assemble_wrapped_chain_is_one_logical_line) {
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", true), rowOf("ef", false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  CHECK_EQ(int(lines.size()), 1);
  CHECK_EQ(int(lines[0].rows.size()), 3);
  CHECK_EQ(lines[0].firstAbsRow, int64_t(0));
  CHECK(lines[0].endsWithNewline);  // last row ended with a hard newline
}

MINI_TEST(assemble_unwrapped_rows_are_independent_lines) {
  std::vector<VisualRow> rows = {rowOf("ab", false), rowOf("cd", false), rowOf("ef", false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  CHECK_EQ(int(lines.size()), 3);
  for (size_t i = 0; i < lines.size(); ++i) {
    CHECK_EQ(int(lines[i].rows.size()), 1);
    CHECK_EQ(lines[i].firstAbsRow, int64_t(i));
    CHECK(lines[i].endsWithNewline);
  }
}

MINI_TEST(assemble_last_row_open_wrap_is_not_truncated) {
  // The final row still wraps (content continues below the passed region):
  // the line is OPEN, but nothing was force-cut.
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", true)};
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(refs.data(), int(refs.size()), 0, 2000, &lines);
  CHECK_EQ(int(lines.size()), 1);
  CHECK_EQ(int(lines[0].rows.size()), 2);
  CHECK(!lines[0].endsWithNewline);
  CHECK_EQ(stats.lineCount, 1);
  CHECK_EQ(stats.truncatedLines, 0);
}

MINI_TEST(assemble_force_cut_at_max_rows_per_line) {
  // 5 wrapped rows, bound 2 → lines of 2/2/1 rows; the first two were
  // force-cut (their last row still wraps and more rows follow).
  std::vector<VisualRow> rows = {
      rowOf("a", true), rowOf("b", true), rowOf("c", true), rowOf("d", true), rowOf("e", false),
  };
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(refs.data(), int(refs.size()), 100, 2, &lines);
  CHECK_EQ(stats.lineCount, 3);
  CHECK_EQ(stats.truncatedLines, 2);
  CHECK_EQ(int(lines.size()), 3);
  CHECK_EQ(int(lines[0].rows.size()), 2);
  CHECK_EQ(int(lines[1].rows.size()), 2);
  CHECK_EQ(int(lines[2].rows.size()), 1);
  CHECK(lines[0].rows.back().wrapped);   // cut edge keeps the fold flag
  CHECK(!lines[0].endsWithNewline);      // the logical line really continues
  CHECK(lines[2].endsWithNewline);
  CHECK_EQ(lines[0].firstAbsRow, int64_t(100));
  CHECK_EQ(lines[1].firstAbsRow, int64_t(102));
  CHECK_EQ(lines[2].firstAbsRow, int64_t(104));
}

MINI_TEST(assemble_clamps_max_rows_per_line) {
  // Bound 0 → 1 row per line.
  std::vector<VisualRow> rows = {rowOf("a", true), rowOf("b", true), rowOf("c", false)};
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(refs.data(), int(refs.size()), 0, 0, &lines);
  CHECK_EQ(stats.lineCount, 3);
  CHECK_EQ(stats.truncatedLines, 2);
  // A bound above the 16-bit packing limit clamps to 65535 (no observable
  // effect on a small input).
  std::vector<LogicalLine> linesBig;
  stats = assemble(refs.data(), int(refs.size()), 0, 100000, &linesBig);
  CHECK_EQ(stats.lineCount, 1);
  CHECK_EQ(stats.truncatedLines, 0);
}

MINI_TEST(assemble_empty_and_null_input) {
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(nullptr, 0, 0, 2000, &lines);
  CHECK_EQ(stats.lineCount, 0);
  CHECK_EQ(stats.truncatedLines, 0);
  CHECK(lines.empty());
  stats = assemble(nullptr, -3, 0, 2000, &lines);
  CHECK_EQ(stats.lineCount, 0);
  // Null out pointer: reported but harmless.
  CHECK_EQ(assemble(nullptr, 0, 0, 2000, nullptr).lineCount, 0);
}

MINI_TEST(extract_text_plain_map_layout) {
  std::vector<VisualRow> rows = {rowOf("ab", true), rowOf("cd", false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("abcd"));
  CHECK_EQ(map.size(), size_t(text.size() + 1));  // contract: size == len + 1
  CHECK_EQ(map[0], pack(0, 0));
  CHECK_EQ(map[1], pack(0, 1));
  CHECK_EQ(map[2], pack(1, 0));  // wrap boundary: byte 2 belongs to row 1
  CHECK_EQ(map[3], pack(1, 1));
  CHECK_EQ(map[4], pack(1, 2));  // one-past-end (after the last cell)
}

MINI_TEST(extract_text_trailing_blanks_trimmed) {
  // Screen::rowText semantics: the last row's right padding is not content.
  std::vector<VisualRow> rows = {rowOf("ab", true), cellsRow(
      {Cell{uint32_t('d'), 0, 0}, Cell{uint32_t(' '), 5, 0}}, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("abd"));
  CHECK_EQ(map.size(), size_t(4));
  CHECK_EQ(map[2], pack(1, 0));
  CHECK_EQ(map[3], pack(1, 1));  // one-past-end of 'd'
}

MINI_TEST(extract_text_wide_lead_maps_lead_column) {
  // a, wide lead (col 1) + trail (col 2), b (col 3).
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
      Cell{uint32_t('b'), 0, 0},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  // "a" + 3-byte UTF-8 of 中 + "b" == 5 bytes.
  CHECK_EQ(text.size(), size_t(5));
  CHECK_EQ(text, std::string("a\xE4\xB8\xAD" "b"));
  CHECK_EQ(map.size(), size_t(6));
  CHECK_EQ(map[0], pack(0, 0));
  // All three bytes of the wide char map to its LEAD column.
  CHECK_EQ(map[1], pack(0, 1));
  CHECK_EQ(map[2], pack(0, 1));
  CHECK_EQ(map[3], pack(0, 1));
  CHECK_EQ(map[4], pack(0, 3));  // 'b' — the trail column is skipped
  CHECK_EQ(map[5], pack(0, 4));  // one-past-end
}

MINI_TEST(extract_text_blank_cells_render_space) {
  // cp 0 (unset) and ' ' both render ' ' — Screen::rowText parity.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('x'), 0, 0},
      Cell{uint32_t(0), 3, 0},
      Cell{uint32_t(' '), 4, 0},
      Cell{uint32_t('y'), 0, 0},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("x  y"));  // x + two blank cells + y
  CHECK_EQ(map.size(), size_t(5));
  CHECK_EQ(map[1], pack(0, 1));
  CHECK_EQ(map[2], pack(0, 2));
  CHECK_EQ(map[4], pack(0, 4));  // one-past-end
}

MINI_TEST(extract_text_multibyte_offsets) {
  // é (2 bytes, col 0), € (3 bytes, col 1), x (col 2).
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0xE9), 0, 0},
      Cell{uint32_t(0x20AC), 0, 0},
      Cell{uint32_t('x'), 0, 0},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("\xC3\xA9\xE2\x82\xAC" "x"));
  CHECK_EQ(map.size(), size_t(7));
  CHECK_EQ(map[0], pack(0, 0));  // é lead byte
  CHECK_EQ(map[1], pack(0, 0));  // é continuation repeats the cluster
  CHECK_EQ(map[2], pack(0, 1));  // € lead byte
  CHECK_EQ(map[3], pack(0, 1));
  CHECK_EQ(map[4], pack(0, 1));
  CHECK_EQ(map[5], pack(0, 2));  // x
  CHECK_EQ(map[6], pack(0, 3));  // one-past-end
}

MINI_TEST(extract_text_all_blank_line_is_empty) {
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(' '), 7, 0},
      Cell{uint32_t(' '), 7, 0},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string(""));
  CHECK_EQ(map.size(), size_t(1));
  CHECK_EQ(map[0], uint32_t(0));  // empty line → (row 0, col 0)
}

MINI_TEST(extract_text_rows_without_cells_are_skipped) {
  // A zero-count row contributes nothing but splits nothing either.
  std::vector<VisualRow> rows = {rowOf("ab", true), cellsRow({}, true), rowOf("cd", false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  CHECK_EQ(int(lines.size()), 1);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("abcd"));
  CHECK_EQ(map[2], pack(2, 0));  // 'c' lives in the third visual row
}

MINI_TEST(extract_text_null_outputs_are_tolerated) {
  std::vector<LogicalLine> lines = assembleAll({rowOf("ab", false)});
  extractText(lines[0], nullptr, nullptr);  // no crash, no effect
}

MINI_TEST(assemble_first_abs_row_offsets) {
  // 6 rows starting at abs row 100: "a"+"b" line, then "c" line, "d" line.
  std::vector<VisualRow> rows = {
      rowOf("a", true), rowOf("b", false), rowOf("c", false), rowOf("d", false),
  };
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(refs.data(), int(refs.size()), 100, 2000, &lines);
  CHECK_EQ(stats.lineCount, 3);
  CHECK_EQ(lines[0].firstAbsRow, int64_t(100));  // "a"+"b" chain
  CHECK_EQ(lines[1].firstAbsRow, int64_t(102));
  CHECK_EQ(lines[2].firstAbsRow, int64_t(103));
}

MINI_TEST(extract_text_one_past_end_after_wide_tail) {
  // A mid-row blank is CONTENT (only trailing blanks trim — rowText parity),
  // so the blank at col 1 renders ' ' between 'a' and 中. The wide lead at
  // col 2 is the last non-blank cell → one-past-end = col 2 + width 2 = 4.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(' '), 0, 0},
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("a \xE4\xB8\xAD"));  // 'a' + blank ' ' + 中
  CHECK_EQ(map.size(), size_t(6));
  CHECK_EQ(map[0], pack(0, 0));
  CHECK_EQ(map[1], pack(0, 1));  // the blank cell at col 1 renders ' '
  CHECK_EQ(map[4], pack(0, 2));  // last byte of 中 still maps the lead col
  CHECK_EQ(map[5], pack(0, 4));  // one-past-end = lead col 2 + width 2
  // Same row without the blank: text "a中", one-past-end col 3.
  rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
  }, false)};
  lines = assembleAll(rows);
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("a\xE4\xB8\xAD"));
  CHECK_EQ(map.size(), size_t(5));
  CHECK_EQ(map[4], pack(0, 3));  // one-past-end = lead col 1 + width 2
}

MINI_TEST(extract_text_orphan_trail_is_skipped) {
  // A trail cell without a lead (defensive — put() prevents it): the text
  // projection skips trails unconditionally, same as Screen::rowText.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(0), 0, kCellWideTrail},
      Cell{uint32_t('b'), 0, 0},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("ab"));
  CHECK_EQ(map.size(), size_t(3));
  CHECK_EQ(map[1], pack(0, 2));  // 'b' — the orphan trail emitted nothing
}

MINI_TEST(extract_text_cjk_chain) {
  // A folded CJK line: every char is 3 bytes, every map entry repeats the
  // cluster position 3 times.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
      Cell{uint32_t(U'文'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
  }, false)};
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("\xE4\xB8\xAD\xE6\x96\x87"));
  CHECK_EQ(map.size(), size_t(7));
  CHECK_EQ(map[0], pack(0, 0));
  CHECK_EQ(map[2], pack(0, 0));
  CHECK_EQ(map[3], pack(0, 2));
  CHECK_EQ(map[5], pack(0, 2));
  CHECK_EQ(map[6], pack(0, 4));
}

MINI_TEST(extract_text_one_past_end_on_earlier_row) {
  // The chain's LAST row is entirely blank padding: trimming leaves 'c' on
  // row 0 as the last non-blank cell → one-past-end lives on an EARLIER row.
  std::vector<VisualRow> rows = {
      rowOf("abc", true),
      cellsRow({Cell{uint32_t(' '), 8, 0}, Cell{uint32_t(' '), 8, 0}}, false),
  };
  std::vector<LogicalLine> lines = assembleAll(rows);
  CHECK_EQ(int(lines.size()), 1);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("abc"));
  CHECK_EQ(map.size(), size_t(4));
  CHECK_EQ(map[2], pack(0, 2));
  CHECK_EQ(map[3], pack(0, 3));  // one-past-end back on row 0
}

MINI_TEST(extract_text_wide_at_chain_boundary) {
  // A wide char ends visual row 0 of a chain; 'y' starts row 1.
  std::vector<VisualRow> rows = {
      cellsRow({
          Cell{uint32_t('x'), 0, 0},
          Cell{uint32_t(U'中'), 0, kCellWideLead},
          Cell{uint32_t(0), 0, kCellWideTrail},
      }, true),
      rowOf("yz", false),
  };
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("x\xE4\xB8\xAD" "yz"));  // 6 bytes
  CHECK_EQ(map.size(), size_t(7));
  CHECK_EQ(map[0], pack(0, 0));  // 'x'
  CHECK_EQ(map[3], pack(0, 1));  // last byte of 中 → its lead col
  CHECK_EQ(map[4], pack(1, 0));  // 'y' — first byte of the next visual row
  CHECK_EQ(map[5], pack(1, 1));  // 'z'
  CHECK_EQ(map[6], pack(1, 2));  // one-past-end
}

MINI_TEST(extract_text_blank_before_wrap_is_content) {
  // A blank cell immediately before a fold boundary is MID-LINE content —
  // only the line's final trailing blanks trim.
  std::vector<VisualRow> rows = {
      cellsRow({Cell{uint32_t('a'), 0, 0}, Cell{uint32_t(' '), 2, 0}}, true),
      rowOf("b", false),
  };
  std::vector<LogicalLine> lines = assembleAll(rows);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  CHECK_EQ(text, std::string("a b"));
  CHECK_EQ(map.size(), size_t(4));
  CHECK_EQ(map[1], pack(0, 1));  // the blank is row 0, col 1
  CHECK_EQ(map[2], pack(1, 0));  // 'b'
  CHECK_EQ(map[3], pack(1, 1));
}

MINI_TEST(extract_text_map_invariants_mixed_rows) {
  // Mixed chain: multibyte row, an EMPTY visual row, then wide + blanks.
  // Invariants: map.size() == text.size() + 1; every entry's (row, col) is
  // in range; entries are non-decreasing in (row, col) order.
  std::vector<VisualRow> rows = {
      cellsRow({Cell{uint32_t(0xE9), 0, 0}, Cell{uint32_t(0x20AC), 0, 0}}, true),
      cellsRow({}, true),  // zero cells — contributes nothing
      cellsRow({
          Cell{uint32_t('a'), 0, 0},
          Cell{uint32_t(' '), 0, 0},
          Cell{uint32_t(U'中'), 0, kCellWideLead},
          Cell{uint32_t(0), 0, kCellWideTrail},
      }, false),
  };
  std::vector<LogicalLine> lines = assembleAll(rows);
  CHECK_EQ(int(lines.size()), 1);
  CHECK_EQ(int(lines[0].rows.size()), 3);
  std::string text;
  std::vector<uint32_t> map;
  extractText(lines[0], &text, &map);
  // "é€" (5 bytes) + "a 中" (5 bytes) == 10 bytes.
  CHECK_EQ(text.size(), size_t(10));
  CHECK_EQ(map.size(), size_t(11));
  CHECK_EQ(map[5], pack(2, 0));  // 'a' — the empty row shifted nothing
  uint32_t prev = 0;
  for (size_t i = 0; i < map.size(); ++i) {
    const uint32_t row = map[i] >> 16;
    const uint32_t col = map[i] & 0xFFFFu;
    CHECK(row < lines[0].rows.size());
    if (i + 1 < map.size()) {
      CHECK(col < uint32_t(lines[0].rows[row].count));  // a real cell
    } else {
      // The one-past-end entry may sit exactly at (or past) the last cell.
      CHECK(col <= uint32_t(lines[0].rows[row].count));
    }
    CHECK(map[i] >= prev);  // stream order is monotone
    prev = map[i];
  }
  CHECK_EQ(map[10], pack(2, 4));  // one-past-end: 中 lead col 2 + width 2
}

MINI_TEST(assemble_alternating_wrap_flags) {
  // wrapped / hard / wrapped: row 1 closes line 1; row 2 OPENS line 2 as an
  // open (still folding) line — not a truncation.
  std::vector<VisualRow> rows = {
      rowOf("a", true), rowOf("b", false), rowOf("c", true),
  };
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  AssembleStats stats = assemble(refs.data(), int(refs.size()), 0, 2000, &lines);
  CHECK_EQ(stats.lineCount, 2);
  CHECK_EQ(stats.truncatedLines, 0);
  CHECK_EQ(int(lines[0].rows.size()), 2);
  CHECK(lines[0].endsWithNewline);   // 'b' ended with a hard newline
  CHECK_EQ(int(lines[1].rows.size()), 1);
  CHECK(!lines[1].endsWithNewline);  // 'c' still folds (open line)
  CHECK_EQ(lines[1].firstAbsRow, int64_t(2));
}

MINI_TEST(assemble_negative_first_abs_row) {
  // Abs row 0 is caller-defined — negative bases must flow through.
  std::vector<VisualRow> rows = {rowOf("a", true), rowOf("b", false)};
  std::vector<RowRef> refs = refsOf(rows);
  std::vector<LogicalLine> lines;
  assemble(refs.data(), int(refs.size()), -5, 2000, &lines);
  CHECK_EQ(int(lines.size()), 1);
  CHECK_EQ(lines[0].firstAbsRow, int64_t(-5));
}

MINI_TEST(extract_text_matches_screen_row_text) {
  // Cross-module parity with the REAL projection: the same cells through
  // Screen::rowText and through extractText (single-row logical line) must
  // agree byte-for-byte, including mid-row blanks and trailing trimming.
  StyleTable styles;
  Screen screen(2, 6, 0, false, styles);
  // Row 0: a, unset cp, é, wide 中 pair, then the Screen's initial blank.
  screen.setCell(0, 0, Cell{uint32_t('a'), 0, 0});
  screen.setCell(0, 1, Cell{uint32_t(0), 0, 0});
  screen.setCell(0, 2, Cell{uint32_t(0xE9), 0, 0});
  screen.setCell(0, 3, Cell{uint32_t(U'中'), 0, kCellWideLead});
  screen.setCell(0, 4, Cell{uint32_t(0), 0, kCellWideTrail});
  LogicalLine line;
  line.rows.push_back(RowRef{screen.rowCells(0), screen.cols(), false, nullptr});
  std::string text;
  std::vector<uint32_t> map;
  extractText(line, &text, &map);
  CHECK_EQ(text, screen.rowText(0));
  CHECK_EQ(text, std::string("a \xC3\xA9\xE4\xB8\xAD"));  // "a é中"
  CHECK_EQ(map.size(), size_t(text.size() + 1));
  // Row 1 stays blank: both projections render empty.
  LogicalLine blank;
  blank.rows.push_back(RowRef{screen.rowCells(1), screen.cols(), false, nullptr});
  extractText(blank, &text, &map);
  CHECK_EQ(text, screen.rowText(1));
  CHECK_EQ(text, std::string(""));
  CHECK_EQ(map.size(), size_t(1));
}
