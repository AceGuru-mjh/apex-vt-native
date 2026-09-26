// Unicode foundation tests — East Asian Width (complete), UAX #29 extended
// grapheme cluster boundaries, emoji properties.
//
// Coverage:
//  * displayWidth spot checks against the Unicode 15.1 EAW data (ASCII,
//    Latin-1, CJK, fullwidth/halfwidth forms, Hangul, emoji, RI width 1);
//  * UAX #29 boundary samples (rules GB1..GB13): >50 sequences covering
//    CR/LF, controls, combining chains, ZWJ emoji families, skin tones,
//    regional-indicator parity, Hangul jamo/syllables, preppends,
//    spacing marks, format characters;
//  * graphemeClass dispatch, Emoji_Presentation / Emoji_Component /
//    ideographic / word-joiner predicates;
//  * GraphemeBreaker::markStarts / markCellStarts (wide lead/trail) and
//    reset() reuse.
#include "mini_test.h"

#include "../src/vt_grapheme.h"
#include "../src/vt_unicode_data.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using namespace apex::vt;
using apex::vt::uni::Gc;

namespace {

// Feeds seq through a fresh breaker; expects exp[i] = boundary before
// seq[i]. The one-call, zero-boilerplate form of a UAX #29 test line.
void checkBreaks(std::initializer_list<uint32_t> seq,
                 std::initializer_list<bool> exp) {
  if (seq.size() != exp.size()) {
    CHECK(!"sample size mismatch");
    return;
  }
  std::vector<uint32_t> cps(seq);
  std::vector<bool> want(exp);
  GraphemeBreaker b;
  for (size_t i = 0; i < cps.size(); ++i) {
    bool got = b.feed(cps[i]);
    if (got != want[i]) {
      // Report position + code points once; CHECK (not CHECK_EQ) so the
      // boolean formatting stays readable.
      CHECK(!"grapheme boundary mismatch");
      return;
    }
  }
  CHECK(true);
}

// Cluster count of a sequence (boundaries + 1 is not used; clusters =
// number of boundary markers).
int clusterCount(std::initializer_list<uint32_t> seq) {
  GraphemeBreaker b;
  int n = 0;
  for (uint32_t cp : seq) {
    if (b.feed(cp)) ++n;
  }
  return n;
}

}  // namespace

// ─── East Asian Width ────────────────────────────────────────────────────

MINI_TEST(width_ascii_and_latin1) {
  using uni::displayWidth;
  CHECK_EQ(displayWidth(0x0041), 1);   // 'A'
  CHECK_EQ(displayWidth(0x0061), 1);   // 'a'
  CHECK_EQ(displayWidth(0x007E), 1);   // '~'
  CHECK_EQ(displayWidth(0x00E9), 1);   // é
  CHECK_EQ(displayWidth(0x00A0), 1);   // NBSP (N)
  CHECK_EQ(displayWidth(0x00B1), 1);   // ± (ambiguous → 1)
  CHECK_EQ(displayWidth(0x00A9), 1);   // © (ambiguous → 1)
  CHECK_EQ(displayWidth(0x2764), 1);   // ❤ EAW N (text presentation heart)
  CHECK_EQ(displayWidth(0x2190), 1);   // ← arrow
  CHECK_EQ(displayWidth(0x2B05), 2);   // ⬅ EAW W
}

MINI_TEST(width_controls_and_combining) {
  using uni::displayWidth;
  CHECK_EQ(displayWidth(0x0000), 0);   // C0
  CHECK_EQ(displayWidth(0x0001), 0);
  CHECK_EQ(displayWidth(0x001F), 0);
  CHECK_EQ(displayWidth(0x007F), 0);   // DEL
  CHECK_EQ(displayWidth(0x009B), 0);   // C1
  CHECK_EQ(displayWidth(0x0301), 0);   // combining acute
  CHECK_EQ(displayWidth(0x036F), 0);
  CHECK_EQ(displayWidth(0x20D0), 0);
  CHECK_EQ(displayWidth(0x1AB0), 0);
  CHECK_EQ(displayWidth(0xFE0F), 0);   // VS16
  CHECK_EQ(displayWidth(0xFE00), 0);   // VS1
  CHECK_EQ(displayWidth(0xE0100), 0);  // MVS
  CHECK_EQ(displayWidth(0x200D), 0);   // ZWJ
  CHECK_EQ(displayWidth(0x200C), 0);   // ZWNJ
  CHECK_EQ(displayWidth(0x200B), 0);   // ZWSP (Cf)
  CHECK_EQ(displayWidth(0x00AD), 0);   // soft hyphen (Cf)
  CHECK_EQ(displayWidth(0x2060), 0);   // word joiner (Cf)
  CHECK_EQ(displayWidth(0x1F3FB), 0);  // skin-tone modifier
  CHECK_EQ(displayWidth(0x0903), 1);   // spacing mark (Mc) keeps its column
  CHECK_EQ(displayWidth(0x06DD), 0);   // Cf prepend
}

MINI_TEST(width_cjk_and_fullwidth) {
  using uni::displayWidth;
  CHECK_EQ(displayWidth(0x4E00), 2);   // CJK Unified
  CHECK_EQ(displayWidth(0x9FFF), 2);
  CHECK_EQ(displayWidth(0x3400), 2);   // Ext A
  CHECK_EQ(displayWidth(0x4DBF), 2);
  CHECK_EQ(displayWidth(0x3000), 2);   // ideographic space
  CHECK_EQ(displayWidth(0x3001), 2);   // 、
  CHECK_EQ(displayWidth(0x303E), 2);
  CHECK_EQ(displayWidth(0x303F), 1);   // halfwidth fill space (N)
  CHECK_EQ(displayWidth(0x3041), 2);   // hiragana
  CHECK_EQ(displayWidth(0x30A2), 2);   // katakana
  CHECK_EQ(displayWidth(0x3105), 2);   // bopomofo
  CHECK_EQ(displayWidth(0x2E80), 2);   // radicals supplement
  CHECK_EQ(displayWidth(0x2F00), 2);   // Kangxi radicals
  CHECK_EQ(displayWidth(0xFF21), 2);   // fullwidth A
  CHECK_EQ(displayWidth(0xFF01), 2);   // fullwidth !
  CHECK_EQ(displayWidth(0xFF60), 2);
  CHECK_EQ(displayWidth(0xFF61), 1);   // halfwidth ideographic stop (H)
  CHECK_EQ(displayWidth(0xFF9E), 1);   // halfwidth voiced mark (H)
  CHECK_EQ(displayWidth(0xFFE0), 2);   // fullwidth cent
  CHECK_EQ(displayWidth(0xF900), 2);   // compat ideographs
  CHECK_EQ(displayWidth(0xA000), 2);   // Yi
  CHECK_EQ(displayWidth(0x20000), 2);  // Ext B
  CHECK_EQ(displayWidth(0x2FFFD), 2);
  CHECK_EQ(displayWidth(0x30000), 2);  // Ext G
  CHECK_EQ(displayWidth(0x3FFFD), 2);
}

MINI_TEST(width_hangul_and_emoji) {
  using uni::displayWidth;
  CHECK_EQ(displayWidth(0xAC00), 2);   // 가 (LV syllable)
  CHECK_EQ(displayWidth(0xD7A3), 2);   // 힣
  CHECK_EQ(displayWidth(0xAC01), 2);   // LVT syllable
  CHECK_EQ(displayWidth(0x1100), 2);   // choseong filler area
  CHECK_EQ(displayWidth(0x115F), 2);
  CHECK_EQ(displayWidth(0x1160), 1);   // jungseong filler (N!)
  CHECK_EQ(displayWidth(0x11A8), 1);   // jongseong (N)
  CHECK_EQ(displayWidth(0xD7B0), 1);   // jamo ext-B vowel (N)
  CHECK_EQ(displayWidth(0x231A), 2);   // watch (W)
  CHECK_EQ(displayWidth(0x231B), 2);
  CHECK_EQ(displayWidth(0x2319), 1);   // broken bar-ish (N)
  CHECK_EQ(displayWidth(0x1F600), 2);  // grinning face
  CHECK_EQ(displayWidth(0x1F300), 2);  // cyclone
  CHECK_EQ(displayWidth(0x1F5FB), 2);  // Mount Fuji
  CHECK_EQ(displayWidth(0x1F321), 1);  // thermometer (N, text presentation)
  CHECK_EQ(displayWidth(0x1F1E6), 1);  // regional indicator A — EAW N!
  CHECK_EQ(displayWidth(0x1F1FF), 1);
  CHECK_EQ(displayWidth(0x1F18E), 2);  // AB button
  CHECK_EQ(displayWidth(0x1F004), 2);  // mahjong red dragon
  CHECK_EQ(displayWidth(0x2B50), 2);   // star
}

// ─── UAX #29 boundary samples ────────────────────────────────────────────

MINI_TEST(grapheme_cr_lf_and_controls) {
  checkBreaks({0x000D, 0x000A}, {true, false});               // GB3 CR×LF
  checkBreaks({0x000D, 0x000D}, {true, true});
  checkBreaks({0x000A, 0x000A}, {true, true});
  checkBreaks({0x000A, 0x0061}, {true, true});                // GB4
  checkBreaks({0x0061, 0x000D}, {true, true});
  checkBreaks({0x0061, 0x000D, 0x0061}, {true, true, true});
  checkBreaks({0x000B, 0x0020}, {true, true});
  checkBreaks({0x0020, 0x0007}, {true, true});                // GB5 ÷Control
  checkBreaks({0x0007, 0x0020}, {true, true});                // GB4 Control÷
  checkBreaks({0x0020, 0x0020}, {true, true});
}

MINI_TEST(grapheme_extend_chains) {
  checkBreaks({0x0020, 0x0308, 0x0020}, {true, false, true});
  checkBreaks({0x0061, 0x0301}, {true, false});
  checkBreaks({0x0061, 0x0301, 0x0062}, {true, false, true});
  checkBreaks({0x0061, 0x0308, 0x0301}, {true, false, false});
  checkBreaks({0x0061, 0x0301, 0x0301, 0x0327}, {true, false, false, false});
  checkBreaks({0x0061, 0xFE0F}, {true, false});               // VS16
  checkBreaks({0x00E9, 0x0301}, {true, false});
  checkBreaks({0x0061, 0x1F3FB}, {true, false});              // skin tone
  checkBreaks({0x0020, 0x200D}, {true, false});               // GB9 ×ZWJ
  checkBreaks({0x0061, 0x200D, 0x0062}, {true, false, true});
}

MINI_TEST(grapheme_zwj_emoji_families) {
  // Family: man + woman + girl via ZWJ — one cluster of 5 code points.
  checkBreaks({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467},
              {true, false, false, false, false});
  CHECK_EQ(clusterCount({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}), 1);
  // Kiss: woman + heart + kiss mark.
  checkBreaks({0x1F469, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B},
              {true, false, false, false, false, false});
  // Handshake across skin tones: GB11 + GB9 interplay.
  checkBreaks({0x1F44D, 0x1F3FB, 0x200D, 0x1F44D, 0x1F3FE},
              {true, false, false, false, false});
  // ExtPict ZWJ ExtPict without history: still joined (GB11 core case).
  checkBreaks({0x1F600, 0x200D, 0x1F600}, {true, false, false});
  // Double ZWJ breaks the GB11 chain: the 2nd ZWJ is not Extend*.
  checkBreaks({0x1F600, 0x200D, 0x200D, 0x1F600}, {true, false, false, true});
  // ZWJ before a non-pictograph starts a new cluster.
  checkBreaks({0x1F600, 0x200D, 0x0061}, {true, false, true});
  // Tag-sequence flag of England: black flag + tags (Extend) + terminator.
  checkBreaks({0x1F3F4, 0xE0067, 0xE0062, 0xE0073, 0xE0063, 0xE0074, 0xE007F},
              {true, false, false, false, false, false, false});
  // Keycap sequence: '1' + FE0F + 20E3.
  checkBreaks({0x0031, 0xFE0F, 0x20E3}, {true, false, false});
}

MINI_TEST(grapheme_regional_indicator_parity) {
  checkBreaks({0x1F1E6, 0x1F1E7}, {true, false});              // US flag
  CHECK_EQ(clusterCount({0x1F1FA, 0x1F1F8}), 1);               // UR flag
  // Odd run → 3rd RI opens a new cluster.
  checkBreaks({0x1F1FA, 0x1F1F8, 0x1F1FA}, {true, false, true});
  // Four RIs = two flags.
  checkBreaks({0x1F1FA, 0x1F1F8, 0x1F1FA, 0x1F1F8},
              {true, false, true, false});
  CHECK_EQ(clusterCount({0x1F1FA, 0x1F1F8, 0x1F1FA, 0x1F1F8}), 2);
  // Text between flags resets the run.
  checkBreaks({0x1F1E6, 0x0061, 0x1F1E7}, {true, true, true});
}

MINI_TEST(grapheme_hangul) {
  checkBreaks({0x1100, 0x1160}, {true, false});                // L × V
  checkBreaks({0x1100, 0x1100}, {true, false});                // L × L
  checkBreaks({0x1100, 0x1160, 0x11A8}, {true, false, false}); // L V T
  checkBreaks({0x1160, 0x11A8}, {true, false});                // V × T
  checkBreaks({0x11A8, 0x11A8}, {true, false});                // T × T
  checkBreaks({0x1100, 0x11A8}, {true, true});                 // L ÷ T
  checkBreaks({0xAC00, 0x11A8}, {true, false});                // LV × T
  checkBreaks({0xAC01, 0x11A8}, {true, false});                // LVT × T
  checkBreaks({0xAC00, 0xAC01}, {true, true});                 // LV ÷ LVT
  checkBreaks({0xAC00, 0x1160}, {true, false});                // LV × V
  checkBreaks({0xAC01, 0x1160}, {true, true});                 // LVT ÷ V
  CHECK_EQ(clusterCount({0x1100, 0x1160, 0x11A8}), 1);
  CHECK_EQ(clusterCount({0xAC01, 0x11A8, 0x11AB}), 1);
}

MINI_TEST(grapheme_prepend) {
  checkBreaks({0x0600, 0x0031}, {true, false});                // GB9b
  checkBreaks({0x06DD, 0x0645}, {true, false});
  checkBreaks({0x0600, 0x0600, 0x0031}, {true, false, false});
  checkBreaks({0x0061, 0x0600}, {true, true});                 // ÷ Prepend
  checkBreaks({0x0600, 0x0031, 0x0061}, {true, false, true});
}

MINI_TEST(grapheme_spacing_mark) {
  checkBreaks({0x0915, 0x0903}, {true, false});                // GB9a
  checkBreaks({0x0915, 0x093E}, {true, false});                // ka + AA
  checkBreaks({0x0915, 0x0903, 0x0915}, {true, false, true});
  checkBreaks({0x0E01, 0x0E33}, {true, false});                // sara am
  checkBreaks({0x0E33, 0x0E01}, {true, true});                 // spacem ÷ base
}

MINI_TEST(grapheme_format_control_chars) {
  // Soft hyphen / ZWSP / word joiner are Control class: own clusters.
  checkBreaks({0x0061, 0x00AD, 0x0062}, {true, true, true});
  checkBreaks({0x0061, 0x200B, 0x0062}, {true, true, true});
  checkBreaks({0x0061, 0x2060, 0x0062}, {true, true, true});
  checkBreaks({0x0061, 0xFEFF, 0x0062}, {true, true, true});
  // Line / paragraph separators likewise.
  checkBreaks({0x0061, 0x2028, 0x0062}, {true, true, true});
}

MINI_TEST(grapheme_first_and_reset) {
  GraphemeBreaker b;
  CHECK(b.feed(0x0061));            // GB1: first is always a boundary
  CHECK(!b.feed(0x0301));
  CHECK(b.feed(0x0062));
  b.reset();
  CHECK(b.feed(0x0061));            // fresh sequence after reset
  CHECK(!b.feed(0x0301));
  // Reuse across different scripts after reset.
  b.reset();
  CHECK(b.feed(0x1F1FA));
  CHECK(!b.feed(0x1F1F8));
  CHECK(b.feed(0x1F1FA));
}

// ─── Stateless pair API ──────────────────────────────────────────────────

MINI_TEST(is_break_stateless) {
  CHECK(GraphemeBreaker::isBreak(0x0061, 0x0062));
  CHECK(!GraphemeBreaker::isBreak(0x000D, 0x000A));            // GB3
  CHECK(!GraphemeBreaker::isBreak(0x0061, 0x0301));            // GB9
  CHECK(!GraphemeBreaker::isBreak(0x0061, 0x200D));            // GB9
  CHECK(!GraphemeBreaker::isBreak(0x1100, 0x1160));            // GB6
  CHECK(!GraphemeBreaker::isBreak(0x1160, 0x11A8));            // GB7
  CHECK(!GraphemeBreaker::isBreak(0xAC01, 0x11A8));            // GB8
  CHECK(!GraphemeBreaker::isBreak(0x0915, 0x0903));            // GB9a
  CHECK(!GraphemeBreaker::isBreak(0x0600, 0x0031));            // GB9b
  CHECK(GraphemeBreaker::isBreak(0x0007, 0x0061));             // GB4
  CHECK(GraphemeBreaker::isBreak(0x0061, 0x0007));             // GB5
  CHECK(!GraphemeBreaker::isBreak(0x1F1FA, 0x1F1F8));          // RI pair (approx)
  CHECK(!GraphemeBreaker::isBreak(0x200D, 0x1F600));           // GB11 (approx)
  CHECK(GraphemeBreaker::isBreak(0x200D, 0x0061));             // ZWJ ÷ letter
  CHECK(GraphemeBreaker::isBreak(0x0061, 0x0600));             // ÷ Prepend
  CHECK(GraphemeBreaker::isBreak(0x0061, 0x1F600));            // plain break
}

// ─── Bulk marking ────────────────────────────────────────────────────────

MINI_TEST(mark_starts_sequences) {
  const uint32_t fam[] = {0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467};
  uint8_t starts[5];
  GraphemeBreaker::markStarts(fam, 5, starts);
  CHECK_EQ(starts[0], 1);
  CHECK_EQ(starts[1], 0);
  CHECK_EQ(starts[2], 0);
  CHECK_EQ(starts[3], 0);
  CHECK_EQ(starts[4], 0);

  const uint32_t crlf[] = {0x000D, 0x000A, 0x0061};
  uint8_t s2[3];
  GraphemeBreaker::markStarts(crlf, 3, s2);
  CHECK_EQ(s2[0], 1);
  CHECK_EQ(s2[1], 0);
  CHECK_EQ(s2[2], 1);

  const uint32_t flags[] = {0x1F1FA, 0x1F1F8, 0x1F1FA, 0x1F1F8};
  uint8_t s3[4];
  GraphemeBreaker::markStarts(flags, 4, s3);
  CHECK_EQ(s3[0], 1);
  CHECK_EQ(s3[1], 0);
  CHECK_EQ(s3[2], 1);
  CHECK_EQ(s3[3], 0);

  const uint32_t hang[] = {0x1100, 0x1160, 0x11A8, 0x0061};
  uint8_t s4[4];
  GraphemeBreaker::markStarts(hang, 4, s4);
  CHECK_EQ(s4[0], 1);
  CHECK_EQ(s4[1], 0);
  CHECK_EQ(s4[2], 0);
  CHECK_EQ(s4[3], 1);
}

MINI_TEST(mark_cell_starts_wide) {
  // a | wide(1F468) trail | wide(1F467) trail | combining
  Cell row[6];
  row[0] = Cell{0x0061, 0, 0};
  row[1] = Cell{0x1F468, 0, kCellWideLead};
  row[2] = Cell{0, 0, kCellWideTrail};
  row[3] = Cell{0x1F467, 0, kCellWideLead};
  row[4] = Cell{0, 0, kCellWideTrail};
  row[5] = Cell{0x0301, 0, 0};  // standalone combining cell (defensive)
  uint8_t starts[6];
  GraphemeBreaker::markCellStarts(row, 6, starts);
  CHECK_EQ(starts[0], 1);   // 'a'
  CHECK_EQ(starts[1], 1);   // wide lead starts a cluster
  CHECK_EQ(starts[2], 0);   // wide trail never starts one
  CHECK_EQ(starts[3], 1);   // adjacent emoji: new cluster
  CHECK_EQ(starts[4], 0);   // trail
  CHECK_EQ(starts[5], 0);   // combining mark continues the cluster

  // Plain narrow row with combining marks.
  Cell row2[3];
  row2[0] = Cell{0x0061, 0, 0};
  row2[1] = Cell{0x0301, 0, 0};
  row2[2] = Cell{0x0062, 0, 0};
  uint8_t s2[3];
  GraphemeBreaker::markCellStarts(row2, 3, s2);
  CHECK_EQ(s2[0], 1);
  CHECK_EQ(s2[1], 0);
  CHECK_EQ(s2[2], 1);

  // CJK wide char row: lead starts, trail does not.
  Cell row3[3];
  row3[0] = Cell{0x0061, 0, 0};
  row3[1] = Cell{0x4E00, 0, kCellWideLead};
  row3[2] = Cell{0, 0, kCellWideTrail};
  uint8_t s3[3];
  GraphemeBreaker::markCellStarts(row3, 3, s3);
  CHECK_EQ(s3[0], 1);
  CHECK_EQ(s3[1], 1);
  CHECK_EQ(s3[2], 0);
}

// ─── Class dispatch + predicates ─────────────────────────────────────────

MINI_TEST(grapheme_class_dispatch) {
  using uni::graphemeClass;
  CHECK(graphemeClass(0x000D) == Gc::kCr);
  CHECK(graphemeClass(0x000A) == Gc::kLf);
  CHECK(graphemeClass(0x0001) == Gc::kControl);
  CHECK(graphemeClass(0x007F) == Gc::kControl);
  CHECK(graphemeClass(0x009B) == Gc::kControl);
  CHECK(graphemeClass(0x00AD) == Gc::kControl);
  CHECK(graphemeClass(0x200B) == Gc::kControl);
  CHECK(graphemeClass(0x2060) == Gc::kControl);
  CHECK(graphemeClass(0x200D) == Gc::kZwj);
  CHECK(graphemeClass(0x0301) == Gc::kExtend);
  CHECK(graphemeClass(0xFE0F) == Gc::kExtend);
  CHECK(graphemeClass(0x1F3FB) == Gc::kExtend);  // Extend wins over ExtPict
  CHECK(graphemeClass(0x200C) == Gc::kExtend);   // ZWNJ
  CHECK(graphemeClass(0xE007F) == Gc::kExtend);  // tag terminator
  CHECK(graphemeClass(0xFF9E) == Gc::kExtend);   // halfwidth voiced mark
  CHECK(graphemeClass(0x1F1E6) == Gc::kRegionalIndicator);
  CHECK(graphemeClass(0x1100) == Gc::kL);
  CHECK(graphemeClass(0xA960) == Gc::kL);
  CHECK(graphemeClass(0x1160) == Gc::kV);
  CHECK(graphemeClass(0xD7B0) == Gc::kV);
  CHECK(graphemeClass(0x11A8) == Gc::kT);
  CHECK(graphemeClass(0xD7CB) == Gc::kT);
  CHECK(graphemeClass(0xAC00) == Gc::kLv);
  CHECK(graphemeClass(0xAC01) == Gc::kLvt);
  CHECK(graphemeClass(0x0600) == Gc::kPrepend);
  CHECK(graphemeClass(0x110BD) == Gc::kPrepend);
  CHECK(graphemeClass(0x0903) == Gc::kSpacingMark);
  CHECK(graphemeClass(0x093E) == Gc::kSpacingMark);
  CHECK(graphemeClass(0x0E33) == Gc::kSpacingMark);
  CHECK(graphemeClass(0x1F600) == Gc::kExtendedPictographic);
  CHECK(graphemeClass(0x2764) == Gc::kExtendedPictographic);
  CHECK(graphemeClass(0x1F1E6) != Gc::kExtendedPictographic);  // RI ≠ ExtPict
  CHECK(graphemeClass(0x0061) == Gc::kOther);
  CHECK(graphemeClass(0x4E00) == Gc::kOther);  // CJK is Other (not ExtPict)
}

MINI_TEST(emoji_presentation_default) {
  using uni::isEmojiPresentationDefault;
  CHECK(isEmojiPresentationDefault(0x231A));
  CHECK(isEmojiPresentationDefault(0x231B));
  CHECK(isEmojiPresentationDefault(0x1F600));
  CHECK(isEmojiPresentationDefault(0x1F300));
  CHECK(isEmojiPresentationDefault(0x1F5FB));
  CHECK(isEmojiPresentationDefault(0x2764));   // heart: text width, emoji face
  CHECK(isEmojiPresentationDefault(0x2B50));
  CHECK(isEmojiPresentationDefault(0x1F5A4));
  CHECK(isEmojiPresentationDefault(0x1F1E6));  // RI (EBP=Yes, width 1)
  CHECK(!isEmojiPresentationDefault(0x2665));  // card suit heart (text)
  CHECK(!isEmojiPresentationDefault(0x00A9));  // ©
  CHECK(!isEmojiPresentationDefault(0x2714));  // heavy check (text)
  CHECK(!isEmojiPresentationDefault(0x2603));  // snowman (text)
  CHECK(!isEmojiPresentationDefault(0x0061));
  CHECK(!isEmojiPresentationDefault(0x1F321)); // thermometer (text)
}

MINI_TEST(emoji_component) {
  using uni::isEmojiComponent;
  CHECK(isEmojiComponent(0x1F3FB));   // skin tone
  CHECK(isEmojiComponent(0x1F3FF));
  CHECK(isEmojiComponent(0x200D));    // ZWJ
  CHECK(isEmojiComponent(0x20E3));    // keycap combiner
  CHECK(isEmojiComponent(0x0030));    // '0'
  CHECK(isEmojiComponent(0x0039));    // '9'
  CHECK(isEmojiComponent(0x0023));    // '#'
  CHECK(isEmojiComponent(0x002A));    // '*'
  CHECK(isEmojiComponent(0x1F1E6));   // RI
  CHECK(isEmojiComponent(0xE0067));   // tag
  CHECK(isEmojiComponent(0x1F468));   // face via EBP
  CHECK(isEmojiComponent(0x2764));    // heart via ExtPict
  CHECK(isEmojiComponent(0x00A9));    // © via ExtPict
  CHECK(!isEmojiComponent(0x0061));
  CHECK(!isEmojiComponent(0x3000));   // CJK space
  CHECK(!isEmojiComponent(0x4E00));
}

MINI_TEST(ideographic_word_selection) {
  using uni::isIdeographic;
  CHECK(isIdeographic(0x2E80));
  CHECK(isIdeographic(0x3000));
  CHECK(isIdeographic(0x303E));
  CHECK(isIdeographic(0x31C0));       // CJK strokes
  CHECK(isIdeographic(0x3400));
  CHECK(isIdeographic(0x4E00));
  CHECK(isIdeographic(0x65E5));       // 日
  CHECK(isIdeographic(0x672C));       // 本
  CHECK(isIdeographic(0x8A9E));       // 語
  CHECK(isIdeographic(0x9FFF));
  CHECK(isIdeographic(0xF900));
  CHECK(isIdeographic(0x20000));
  CHECK(isIdeographic(0x2A6DF));
  CHECK(isIdeographic(0x30000));
  CHECK(!isIdeographic(0x303F));
  CHECK(!isIdeographic(0x3041));      // kana excluded
  CHECK(!isIdeographic(0x30A2));
  CHECK(!isIdeographic(0xAC00));      // Hangul excluded
  CHECK(!isIdeographic(0xFF21));      // fullwidth Latin excluded
  CHECK(!isIdeographic(0x0061));
}

MINI_TEST(regional_indicator_and_word_joiner) {
  using uni::isRegionalIndicator;
  CHECK(isRegionalIndicator(0x1F1E6));
  CHECK(isRegionalIndicator(0x1F1FA));
  CHECK(isRegionalIndicator(0x1F1FF));
  CHECK(!isRegionalIndicator(0x1F1E5));
  CHECK(!isRegionalIndicator(0x1F200));
  CHECK(!isRegionalIndicator(0x0061));

  using uni::isWordJoiner;
  CHECK(isWordJoiner(0x2060));
  CHECK(isWordJoiner(0xFEFF));
  CHECK(!isWordJoiner(0x200B));   // ZWSP is a space, not a joiner
  CHECK(!isWordJoiner(0x200D));   // ZWJ is its own thing
  CHECK(!isWordJoiner(0x2066));
}

MINI_TEST(unicode_version_string) {
  CHECK_EQ(std::string(uni::unicodeVersion()), std::string("15.1.0"));
}

// ─── Consistency: width vs. cluster model on representative text ─────────

MINI_TEST(width_matches_grapheme_on_row_projection) {
  // A rendered row "漢字 + family emoji" — every wide lead occupies 2
  // columns and its trail cell is never a cluster start; the emoji family
  // contributes exactly 2 columns (lead + trail) for 5 code points.
  const uint32_t row[] = {0x6F22, 0x5B57, 0x1F468, 0x200D, 0x1F469};
  uint8_t starts[5];
  GraphemeBreaker::markStarts(row, 5, starts);
  CHECK_EQ(starts[0], 1);   // 漢
  CHECK_EQ(starts[1], 1);   // 字
  CHECK_EQ(starts[2], 1);   // family lead
  CHECK_EQ(starts[3], 0);   // ZWJ
  CHECK_EQ(starts[4], 0);   // woman
  int columns = 0;
  for (uint32_t cp : row) columns += uni::displayWidth(cp);
  CHECK_EQ(columns, 2 + 2 + 2 + 0 + 2);  // 漢 字 family(one glyph, 2 cols)
}
