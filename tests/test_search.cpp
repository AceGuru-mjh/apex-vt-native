// Search tests — case folding, KMP matching across wrap boundaries, hit →
// (row, col) mapping via the lines::extractText map, wholeWord, bounds.
#include "mini_test.h"

#include "../src/vt_lines.h"
#include "../src/vt_search.h"

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

VisualRow rowOf(const char* text, bool wrapped) {
  VisualRow r;
  r.wrapped = wrapped;
  for (const char* p = text; *p != '\0'; ++p) {
    r.cells.push_back(Cell{uint32_t(*p), 0, 0});
  }
  return r;
}

VisualRow cellsRow(std::vector<Cell> cells, bool wrapped) {
  VisualRow r;
  r.cells = std::move(cells);
  r.wrapped = wrapped;
  return r;
}

SearchQuery query(std::string pattern, bool caseInsensitive = true,
                  bool wholeWord = false, int maxHits = 2000) {
  SearchQuery q;
  q.pattern = std::move(pattern);
  q.caseInsensitive = caseInsensitive;
  q.wholeWord = wholeWord;
  q.maxHits = maxHits;
  return q;
}

// Full pipeline: visual rows → assemble → extractText → addLine → hits.
std::vector<SearchHit> searchRows(const SearchQuery& q, std::vector<VisualRow> rows) {
  std::vector<RowRef> refs;
  refs.reserve(rows.size());
  for (const VisualRow& r : rows) refs.push_back(r.ref());
  std::vector<lines::LogicalLine> lines;
  lines::assemble(refs.data(), int(refs.size()), 0, 2000, &lines);
  SearchEngine engine;
  engine.reset(q);
  std::string text;
  std::vector<uint32_t> map;
  for (const lines::LogicalLine& line : lines) {
    lines::extractText(line, &text, &map);
    engine.addLine(text, map.data());
  }
  return engine.hits();
}

uint32_t pack(int row, int col) { return (uint32_t(row) << 16) | uint32_t(col); }

std::string folded(std::string_view in) {
  std::string out;
  SearchEngine::foldUtf8(in, &out);
  return out;
}

}  // namespace

MINI_TEST(search_case_insensitive_ascii) {
  std::vector<SearchHit> hits = searchRows(query("hello"), {rowOf("Hello World", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].lineId, int32_t(0));
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endRow, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(5));
  hits = searchRows(query("wOrLd"), {rowOf("Hello World", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(6));
  CHECK_EQ(hits[0].endCol, int32_t(11));
}

MINI_TEST(search_case_sensitive_miss_and_hit) {
  CHECK_EQ(int(searchRows(query("hello", false), {rowOf("Hello World", false)}).size()), 0);
  std::vector<SearchHit> hits = searchRows(query("Hello", false), {rowOf("Hello World", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(0));
}

MINI_TEST(search_match_across_wrap_boundary) {
  // "ab" + "cd" folded into one logical line: "bc" spans the fold.
  std::vector<SearchHit> hits =
      searchRows(query("bc"), {rowOf("ab", true), rowOf("cd", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(1));  // 'b' on visual row 0
  CHECK_EQ(hits[0].endRow, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(1));    // one-past 'c' on visual row 1
}

MINI_TEST(search_full_span_hits_one_past_end) {
  std::vector<SearchHit> hits =
      searchRows(query("abcd"), {rowOf("ab", true), rowOf("cd", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endRow, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(2));    // map one-past-end entry
}

MINI_TEST(search_whole_word_excludes_substring) {
  // "cat" standalone hits; the "cat" inside "catalog" does not.
  std::vector<SearchHit> hits =
      searchRows(query("cat", true, true), {rowOf("cat catalog", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(3));
  // Substring only → no hit at all.
  CHECK_EQ(int(searchRows(query("cat", true, true), {rowOf("concatenate", false)}).size()), 0);
  // Word chars on both sides.
  CHECK_EQ(int(searchRows(query("at", true, true), {rowOf("cat catalog", false)}).size()), 0);
}

MINI_TEST(search_whole_word_line_boundaries_count) {
  // Logical line start/end is a word boundary; mid-line spaces are too.
  std::vector<SearchHit> hits = searchRows(
      query("foo", true, true),
      {rowOf("foo", false), rowOf("xfoox", false), rowOf("foo bar", false)});
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].lineId, int32_t(0));
  CHECK_EQ(hits[1].lineId, int32_t(2));
  CHECK_EQ(hits[1].startCol, int32_t(0));
}

MINI_TEST(search_whole_word_underscore_and_digits) {
  CHECK(SearchEngine::isWordChar('_'));
  CHECK(SearchEngine::isWordChar('9'));
  CHECK(!SearchEngine::isWordChar('-'));
  CHECK(!SearchEngine::isWordChar(' '));
  // "foo_bar" is one word — "foo" is not a whole-word hit inside it.
  CHECK_EQ(int(searchRows(query("foo", true, true), {rowOf("foo_bar", false)}).size()), 0);
}

MINI_TEST(search_max_hits_truncates) {
  std::vector<VisualRow> rows = {
      rowOf("foo", false), rowOf("foo", false), rowOf("foo", false), rowOf("foo", false),
  };
  std::vector<SearchHit> hits = searchRows(query("foo", true, false, 2), rows);
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].lineId, int32_t(0));
  CHECK_EQ(hits[1].lineId, int32_t(1));
}

MINI_TEST(search_empty_pattern_no_hits) {
  CHECK_EQ(int(searchRows(query(""), {rowOf("anything", false)}).size()), 0);
  // maxHits <= 0 → no hits either.
  CHECK_EQ(int(searchRows(query("any", true, false, 0), {rowOf("anything", false)}).size()), 0);
}

MINI_TEST(search_pattern_longer_than_line) {
  CHECK_EQ(int(searchRows(query("abc"), {rowOf("ab", false)}).size()), 0);
}

MINI_TEST(search_overlapping_hits) {
  std::vector<SearchHit> hits = searchRows(query("aa"), {rowOf("aaa", false)});
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(2));
  CHECK_EQ(hits[1].startCol, int32_t(1));
  CHECK_EQ(hits[1].endCol, int32_t(3));
}

MINI_TEST(search_hit_coordinates_precise) {
  // Two visual rows, one logical line: "abcd" + "efgh".
  std::vector<SearchHit> hits =
      searchRows(query("ef"), {rowOf("abcd", true), rowOf("efgh", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(1));
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endRow, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(2));
  // "de" spans the fold: d at (0,3), e at (1,0).
  hits = searchRows(query("de"), {rowOf("abcd", true), rowOf("efgh", false)});
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(3));
  CHECK_EQ(hits[0].endRow, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(1));
}

MINI_TEST(search_multi_line_hit_order) {
  std::vector<SearchHit> hits = searchRows(
      query("foo"),
      {rowOf("foo", false), rowOf("bar", false), rowOf("foo foo", false)});
  CHECK_EQ(int(hits.size()), 3);
  CHECK_EQ(hits[0].lineId, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[1].lineId, int32_t(2));
  CHECK_EQ(hits[1].startCol, int32_t(0));
  CHECK_EQ(hits[2].lineId, int32_t(2));
  CHECK_EQ(hits[2].startCol, int32_t(4));
  CHECK_EQ(hits[2].endCol, int32_t(7));
}

MINI_TEST(search_greek_fold_and_match) {
  // ΑΒΓ (CE 91 CE 92 CE 93) folds to αβγ (CE B1 CE B2 CE B3).
  CHECK_EQ(folded("\xCE\x91\xCE\x92\xCE\x93"), std::string("\xCE\xB1\xCE\xB2\xCE\xB3"));
  CHECK_EQ(folded("\xCE\x8C"), std::string("\xCF\x8C"));   // Ό (U+038C) → ό (U+03CC)
  CHECK_EQ(folded("\xCF\x82"), std::string("\xCF\x83"));   // ς → σ
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0x391), 0, 0},
      Cell{uint32_t(0x392), 0, 0},
      Cell{uint32_t(0x393), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("\xCE\xB1\xCE\xB2\xCE\xB3"), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(3));
}

MINI_TEST(search_cyrillic_fold_and_match) {
  // АБВ (D0 90 D0 91 D0 92) folds to абв (D0 B0 D0 B1 D0 B2).
  CHECK_EQ(folded("\xD0\x90\xD0\x91\xD0\x92"), std::string("\xD0\xB0\xD0\xB1\xD0\xB2"));
  CHECK_EQ(folded("\xD0\x80"), std::string("\xD1\x90"));   // Ѐ (U+0400) → ѐ (U+0450)
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0x410), 0, 0},
      Cell{uint32_t(0x411), 0, 0},
      Cell{uint32_t(0x412), 0, 0},
  }, false)};
  CHECK_EQ(int(searchRows(query("\xD0\xB0\xD0\xB1\xD0\xB2"), rows).size()), 1);
  // Case-sensitive multibyte: no fold → no hit.
  CHECK_EQ(int(searchRows(query("\xD0\xB0", false), rows).size()), 0);
}

MINI_TEST(search_latin1_and_latin_ext_fold) {
  CHECK_EQ(folded("\xC3\x80\xC3\x89\xC3\x8E"), std::string("\xC3\xA0\xC3\xA9\xC3\xAE"));
  CHECK_EQ(folded("\xC5\xB8"), std::string("\xC3\xBF"));   // Ÿ → ÿ
  CHECK_EQ(folded("\xC4\x80\xC4\x92"), std::string("\xC4\x81\xC4\x93"));  // ĀĒ → āē
}

MINI_TEST(search_fold_is_length_preserving) {
  // Length-changing folds stay untouched — offsets must stay 1:1.
  CHECK_EQ(folded("\xC3\x9F"), std::string("\xC3\x9F"));   // ß kept
  CHECK_EQ(folded("\xC4\xB0"), std::string("\xC4\xB0"));   // İ kept
  const std::string in = "Ab\xC3\x9F\xC4\xB0\xCE\x91z";
  CHECK_EQ(folded(in).size(), in.size());
  // Invalid / truncated UTF-8 passes bytes through verbatim.
  CHECK_EQ(folded("\xC3"), std::string("\xC3"));
  CHECK_EQ(folded("a\x80" "b"), std::string("a\x80" "b"));
}

MINI_TEST(search_cluster_alignment_mid_cluster_end) {
  // Text "xéy" (61 C3 A9 79). Pattern "x\xC3" matches the 'x' plus the
  // FIRST byte of é — the hit owns the whole é cell → end col 2.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('x'), 0, 0},
      Cell{uint32_t(0xE9), 0, 0},
      Cell{uint32_t('y'), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("x\xC3", true, false, 2000), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(2));  // aligned past the é cell
}

MINI_TEST(search_wide_char_hit_maps_lead) {
  // a, wide 中 (lead col 1 + trail col 2), b → text "a中b".
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
      Cell{uint32_t('b'), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("\xE4\xB8\xAD", true, false, 2000), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(1));  // lead column
  CHECK_EQ(hits[0].endCol, int32_t(3));    // one-past the wide cell (lead 1 + width 2)
}

MINI_TEST(search_null_map_counts_line_but_skips_matching) {
  // map == nullptr: the line still consumes a lineId.
  SearchEngine engine;
  engine.reset(query("foo"));
  engine.addLine("foo", nullptr);
  // Hand-built map for "foo": cols 0..2 + one-past-end.
  const uint32_t map[4] = {pack(0, 0), pack(0, 1), pack(0, 2), pack(0, 3)};
  engine.addLine("foo", map);
  CHECK_EQ(int(engine.hits().size()), 1);
  CHECK_EQ(engine.hits()[0].lineId, int32_t(1));  // first call consumed id 0
}

MINI_TEST(search_reset_reuses_engine) {
  SearchEngine engine;
  engine.reset(query("ab"));
  engine.addLine("ab", nullptr);
  CHECK_EQ(int(engine.hits().size()), 0);
  engine.reset(query("zz"));
  const uint32_t map[3] = {pack(0, 0), pack(0, 1), pack(0, 2)};
  engine.addLine("zz", map);
  CHECK_EQ(int(engine.hits().size()), 1);
  CHECK_EQ(engine.hits()[0].lineId, int32_t(0));  // ids restart after reset
}

MINI_TEST(search_armenian_and_vietnamese_fold) {
  // Armenian Ա (U+0531) → ա (U+0561); Բ (U+0532) → բ (U+0562).
  CHECK_EQ(folded("\xD4\xB1\xD4\xB2"), std::string("\xD5\xA1\xD5\xA2"));
  // Vietnamese Ạ (U+1EA0) → ạ (U+1EA1); Ằ (U+1EB0) → ằ (U+1EB1).
  CHECK_EQ(folded("\xE1\xBA\xA0"), std::string("\xE1\xBA\xA1"));
  CHECK_EQ(folded("\xE1\xBA\xB0"), std::string("\xE1\xBA\xB1"));
  // ẞ (U+1E9E) is excluded (its lowercase ß would shorten 3→2 bytes).
  CHECK_EQ(folded("\xE1\xBA\x9E"), std::string("\xE1\xBA\x9E"));
}

MINI_TEST(search_vietnamese_match_via_fold) {
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0x1EA0), 0, 0},  // Ạ
      Cell{uint32_t(0x1EA1), 0, 0},  // ạ
      Cell{uint32_t(0x1EA0), 0, 0},  // Ạ
  }, false)};
  // Pattern "ạạ" (U+1EA1 U+1EA1 = E1 BA A1 E1 BA A1) folds onto the folded
  // text "ạạạ" → 2 overlapping hits.
  std::vector<SearchHit> hits = searchRows(query("\xE1\xBA\xA1\xE1\xBA\xA1"), rows);
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(2));
  CHECK_EQ(hits[1].startCol, int32_t(1));
  CHECK_EQ(hits[1].endCol, int32_t(3));
}

MINI_TEST(search_greek_across_wrap_boundary) {
  // "ΑΒ" + "ΓΔ" folded into one logical line; match "βγ" (lowercase) which
  // spans the fold — both sides fold to lowercase.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0x391), 0, 0},
      Cell{uint32_t(0x392), 0, 0},
  }, true), cellsRow({
      Cell{uint32_t(0x393), 0, 0},
      Cell{uint32_t(0x394), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("\xCE\xB2\xCE\xB3"), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(1));  // Β on visual row 0
  CHECK_EQ(hits[0].endRow, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(1));    // one-past Γ on visual row 1
}

MINI_TEST(search_max_hits_stops_mid_line) {
  // 3 hits in ONE line, maxHits 2 → the third is dropped.
  std::vector<SearchHit> hits = searchRows(query("a", true, false, 2), {rowOf("aba", false)});
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[1].startCol, int32_t(2));
}

MINI_TEST(search_line_ids_advance_after_exhaustion) {
  SearchEngine engine;
  engine.reset(query("x", true, false, 1));
  const uint32_t map0[2] = {pack(0, 0), pack(0, 1)};
  engine.addLine("x", map0);  // exhausts maxHits
  const uint32_t map1[2] = {pack(0, 0), pack(0, 1)};
  engine.addLine("x", map1);  // counted, not matched
  CHECK_EQ(int(engine.hits().size()), 1);
  CHECK_EQ(engine.hits()[0].lineId, int32_t(0));
}

MINI_TEST(search_empty_line_consumes_line_id) {
  SearchEngine engine;
  engine.reset(query("ab"));
  engine.addLine("", nullptr);  // empty logical line (all blanks)
  const uint32_t map[3] = {pack(0, 0), pack(0, 1), pack(0, 2)};
  engine.addLine("ab", map);
  CHECK_EQ(int(engine.hits().size()), 1);
  CHECK_EQ(engine.hits()[0].lineId, int32_t(1));
}

MINI_TEST(search_kmp_prefix_overlaps) {
  // "abab" in "ababab" — the failure table must slide, not restart.
  std::vector<SearchHit> hits = searchRows(query("abab"), {rowOf("ababab", false)});
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[1].startCol, int32_t(2));
}

MINI_TEST(search_whole_word_non_word_pattern) {
  // Pattern of non-word chars: only adjacency to word chars rejects.
  // "a++" (word char before) and "++b" (word char after) are rejected; the
  // standalone "++" between spaces hits; inside "+++" EVERY overlapping
  // pair has non-word neighbors → 2 hits (cols 11 and 12).
  std::vector<SearchHit> hits =
      searchRows(query("++", true, true), {rowOf("a++ ++ ++b +++", false)});
  CHECK_EQ(int(hits.size()), 3);
  CHECK_EQ(hits[0].startCol, int32_t(4));   // the standalone "++"
  CHECK_EQ(hits[1].startCol, int32_t(11));  // "+++" pair one (cols 11-12)
  CHECK_EQ(hits[2].startCol, int32_t(12));  // "+++" pair two (cols 12-13)
}

MINI_TEST(search_fold_greek_extended_polytonic) {
  // U+1F08 (Ἀ) → U+1F00 (ἀ); U+1FBA (Ὰ) → U+1F70 (ὰ); U+1FEC (Ῥ) → U+1FE5.
  CHECK_EQ(folded("\xE1\xBC\x88"), std::string("\xE1\xBC\x80"));
  CHECK_EQ(folded("\xE1\xBE\xBA"), std::string("\xE1\xBD\xB0"));
  CHECK_EQ(folded("\xE1\xBF\xAC"), std::string("\xE1\xBF\xA5"));
  // Greek Ext is 3-byte → 3-byte: total length never moves.
  const std::string in = "\xE1\xBC\x88\xE1\xBE\xBA\xE1\xBF\xAC";
  CHECK_EQ(folded(in).size(), in.size());
}

MINI_TEST(search_greek_extended_match) {
  // Cells hold ἈἉἈἉ (U+1F08/U+1F09 twice); the lowercase pattern ἀἁ (U+1F00,
  // U+1F01) matches twice through the fold (non-overlapping, cols 0 and 2).
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0x1F08), 0, 0},
      Cell{uint32_t(0x1F09), 0, 0},
      Cell{uint32_t(0x1F08), 0, 0},
      Cell{uint32_t(0x1F09), 0, 0},
  }, false)};
  std::vector<SearchHit> hits =
      searchRows(query("\xE1\xBC\x80\xE1\xBC\x81"), rows);
  CHECK_EQ(int(hits.size()), 2);
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(2));
  CHECK_EQ(hits[1].startCol, int32_t(2));
  CHECK_EQ(hits[1].endCol, int32_t(4));
}

MINI_TEST(search_whole_word_multibyte_neighbors) {
  // é f o o é: the multibyte neighbors are NOT ASCII word chars, so "foo"
  // counts as a whole word despite touching non-ASCII text.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t(0xE9), 0, 0},
      Cell{uint32_t('f'), 0, 0},
      Cell{uint32_t('o'), 0, 0},
      Cell{uint32_t('o'), 0, 0},
      Cell{uint32_t(0xE9), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("foo", true, true), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(1));
  CHECK_EQ(hits[0].endCol, int32_t(4));
}

MINI_TEST(search_hit_end_inside_wide_cluster_aligns) {
  // "a中b": pattern "a" + first byte of 中 ends INSIDE the 3-byte wide
  // cluster → alignForward owns the whole cell → end col 3 (lead 1 + 2).
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t(U'中'), 0, kCellWideLead},
      Cell{uint32_t(0), 0, kCellWideTrail},
      Cell{uint32_t('b'), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("a\xE4"), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startRow, int32_t(0));
  CHECK_EQ(hits[0].startCol, int32_t(0));
  CHECK_EQ(hits[0].endRow, int32_t(0));
  CHECK_EQ(hits[0].endCol, int32_t(3));
}

MINI_TEST(search_hit_start_mid_cluster_maps_to_cluster_cell) {
  // Pattern "\xA9y" starts at é's continuation byte: the start maps to é's
  // own cell (the cluster owns its bytes), and the hit runs to line end.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('x'), 0, 0},
      Cell{uint32_t(0xE9), 0, 0},
      Cell{uint32_t('y'), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("\xA9y"), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(1));  // é's cell
  CHECK_EQ(hits[0].endCol, int32_t(3));    // one-past 'y'
}

MINI_TEST(search_fold_4byte_emoji_unchanged) {
  // Emoji have no case — 4-byte sequences pass through byte-identical.
  const std::string in = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
  CHECK_EQ(folded(in), in);
  // Mixed: 1+2+3+4 byte classes fold only where a table entry exists.
  const std::string mixed = "a\xC3\x89\xE1\xBC\x88\xF0\x9F\x98\x80";
  CHECK_EQ(folded(mixed).size(), mixed.size());
}

MINI_TEST(search_latin1_hit_through_fold) {
  // "café" + pattern "É" (capital) → hits the é at col 3.
  std::vector<VisualRow> rows = {cellsRow({
      Cell{uint32_t('c'), 0, 0},
      Cell{uint32_t('a'), 0, 0},
      Cell{uint32_t('f'), 0, 0},
      Cell{uint32_t(0xE9), 0, 0},
  }, false)};
  std::vector<SearchHit> hits = searchRows(query("\xC3\x89"), rows);
  CHECK_EQ(int(hits.size()), 1);
  CHECK_EQ(hits[0].startCol, int32_t(3));
  CHECK_EQ(hits[0].endCol, int32_t(4));
}

MINI_TEST(search_reset_to_empty_pattern_disarms) {
  SearchEngine engine;
  engine.reset(query("ab"));
  const uint32_t map[3] = {pack(0, 0), pack(0, 1), pack(0, 2)};
  engine.addLine("ab", map);
  CHECK_EQ(int(engine.hits().size()), 1);
  engine.reset(query(""));
  engine.addLine("ab", map);
  CHECK_EQ(int(engine.hits().size()), 0);
  CHECK_EQ(engine.hits().empty(), true);
}
