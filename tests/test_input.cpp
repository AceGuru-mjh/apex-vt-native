// Input-encoding tests — byte-exact xterm contract checks for vt_input.
// Every assertion pins the full byte sequence (no substring matching), so a
// single off-by-one in a table surfaces immediately.
#include "mini_test.h"

#include "../src/vt_input.h"

#include <cstdint>
#include <vector>

using namespace apex::vt::input;

namespace {

using Bytes = std::vector<uint8_t>;

// ESC + ASCII payload.
Bytes esc(const char* s) {
  Bytes b;
  b.push_back(0x1b);
  while (*s != '\0') b.push_back(uint8_t(*s++));
  return b;
}

// Raw byte list.
Bytes raw(std::initializer_list<uint8_t> bs) { return Bytes(bs); }

// Plain ASCII string as bytes (no ESC).
Bytes str(const char* s) {
  Bytes b;
  while (*s != '\0') b.push_back(uint8_t(*s++));
  return b;
}

// CHECK_EQ for byte vectors with a hex dump on mismatch (mini_test has no
// vector printer).
void checkBytes(const Bytes& actual, const Bytes& expected) {
  ++::minitest::checks();
  if (actual == expected) return;
  ++::minitest::failures();
  std::printf("  FAIL %s:%d  byte mismatch in %s\n    actual:   ", __FILE__,
              __LINE__, ::minitest::current());
  for (uint8_t b : actual) std::printf("%02x ", b);
  std::printf("\n    expected: ");
  for (uint8_t b : expected) std::printf("%02x ", b);
  std::printf("\n");
}

Modifiers mShift() { Modifiers m; m.shift = true; return m; }
Modifiers mCtrl() { Modifiers m; m.ctrl = true; return m; }
Modifiers mAlt() { Modifiers m; m.alt = true; return m; }
Modifiers mCtrlAlt() { Modifiers m; m.ctrl = true; m.alt = true; return m; }
Modifiers mMeta() { Modifiers m; m.meta = true; return m; }

InputModes plain() { return InputModes{}; }
InputModes decckm() { InputModes i; i.applicationCursorKeys = true; return i; }
InputModes deckpam() { InputModes i; i.applicationKeypad = true; return i; }
InputModes level(ModifyLevel l) {
  InputModes i;
  i.modifyLevel = l;
  return i;
}

Bytes key(Key k, Modifiers m = Modifiers{}, InputModes i = InputModes{}) {
  return encodeKey(k, m, i);
}

}  // namespace

MINI_TEST(cursor_keys_normal_vs_decckm) {
  checkBytes(key(Key::kUp), esc("[A"));
  checkBytes(key(Key::kDown), esc("[B"));
  checkBytes(key(Key::kRight), esc("[C"));
  checkBytes(key(Key::kLeft), esc("[D"));
  checkBytes(key(Key::kHome), esc("[H"));
  checkBytes(key(Key::kEnd), esc("[F"));
  // DECCKM: arrows + Home/End switch to SS3 finals.
  checkBytes(key(Key::kUp, Modifiers{}, decckm()), esc("OA"));
  checkBytes(key(Key::kDown, Modifiers{}, decckm()), esc("OB"));
  checkBytes(key(Key::kRight, Modifiers{}, decckm()), esc("OC"));
  checkBytes(key(Key::kLeft, Modifiers{}, decckm()), esc("OD"));
  checkBytes(key(Key::kHome, Modifiers{}, decckm()), esc("OH"));
  checkBytes(key(Key::kEnd, Modifiers{}, decckm()), esc("OF"));
}

MINI_TEST(editing_keys_csi_tilde) {
  checkBytes(key(Key::kInsert), esc("[2~"));
  checkBytes(key(Key::kDelete), esc("[3~"));
  checkBytes(key(Key::kPageUp), esc("[5~"));
  checkBytes(key(Key::kPageDown), esc("[6~"));
  // DECCKM does not affect the tilde block.
  checkBytes(key(Key::kInsert, Modifiers{}, decckm()), esc("[2~"));
}

MINI_TEST(modified_arrows_parameterized) {
  // Modifier parameter = 1 + shift + alt*2 + ctrl*4 + meta*8.
  checkBytes(key(Key::kUp, mShift()), esc("[1;2A"));
  checkBytes(key(Key::kRight, mCtrlAlt()), esc("[1;7C"));  // 1+2+4
  checkBytes(key(Key::kDown, mMeta()), esc("[1;9B"));
  checkBytes(key(Key::kHome, mShift()), esc("[1;2H"));
  checkBytes(key(Key::kEnd, mCtrl()), esc("[1;5F"));
  // Modifiers override DECCKM (parameterized CSI form, as xterm does).
  checkBytes(key(Key::kUp, mShift(), decckm()), esc("[1;2A"));
  checkBytes(key(Key::kLeft, mShift()), esc("[1;2D"));
}

MINI_TEST(modified_tilde_keys) {
  checkBytes(key(Key::kPageUp, mShift()), esc("[5;2~"));
  checkBytes(key(Key::kDelete, mCtrl()), esc("[3;5~"));
  checkBytes(key(Key::kInsert, mCtrlAlt()), esc("[2;7~"));
}

MINI_TEST(function_keys_f1_f4_ss3) {
  checkBytes(key(Key::kF1), esc("OP"));
  checkBytes(key(Key::kF2), esc("OQ"));
  checkBytes(key(Key::kF3), esc("OR"));
  checkBytes(key(Key::kF4), esc("OS"));
  checkBytes(key(Key::kF1, mShift()), esc("[1;2P"));
  checkBytes(key(Key::kF3, mCtrlAlt()), esc("[1;7R"));
}

MINI_TEST(function_keys_f5_f12_tilde) {
  checkBytes(key(Key::kF5), esc("[15~"));
  checkBytes(key(Key::kF6), esc("[17~"));
  checkBytes(key(Key::kF7), esc("[18~"));
  checkBytes(key(Key::kF8), esc("[19~"));
  checkBytes(key(Key::kF9), esc("[20~"));
  checkBytes(key(Key::kF10), esc("[21~"));
  checkBytes(key(Key::kF11), esc("[23~"));
  checkBytes(key(Key::kF12), esc("[24~"));
  checkBytes(key(Key::kF5, mShift()), esc("[15;2~"));
  checkBytes(key(Key::kF12, mCtrl()), esc("[24;5~"));
}

MINI_TEST(text_keys_unmodified) {
  checkBytes(key(Key::kEnter), raw({0x0d}));
  checkBytes(key(Key::kTab), raw({0x09}));
  checkBytes(key(Key::kBackspace), raw({0x7f}));
  checkBytes(key(Key::kEscape), raw({0x1b}));
  checkBytes(key(Key::kSpace), raw({0x20}));
}

MINI_TEST(text_key_modifiers_legacy) {
  checkBytes(key(Key::kTab, mShift()), esc("[Z"));      // back-tab
  checkBytes(key(Key::kBackspace, mCtrl()), raw({0x08}));
  checkBytes(key(Key::kSpace, mCtrl()), raw({0x00}));
  checkBytes(key(Key::kSpace, mAlt()), raw({0x1b, 0x20}));
  checkBytes(key(Key::kEnter, mCtrl()), raw({0x0a}));   // xterm: Ctrl+Ret = LF
  checkBytes(key(Key::kEnter, mAlt()), raw({0x1b, 0x0d}));
  checkBytes(key(Key::kEscape, mCtrl()), raw({0x1b}));  // modifiers dropped
}

MINI_TEST(meta_behaves_as_alt_prefix) {
  // xterm legacy: meta shares the ESC-prefix path of alt.
  checkBytes(key(Key::kBackspace, mMeta()), raw({0x1b, 0x7f}));
  checkBytes(key(Key::kTab, mMeta()), raw({0x1b, 0x09}));
  checkBytes(key(Key::kSpace, mMeta()), raw({0x1b, 0x20}));
}

MINI_TEST(keypad_full_table_enumeration) {
  static const struct { Key k; char num; char app; } expected[] = {
      {Key::kNumpad0, '0', 'p'}, {Key::kNumpad1, '1', 'q'}, {Key::kNumpad2, '2', 'r'},
      {Key::kNumpad3, '3', 's'}, {Key::kNumpad4, '4', 't'}, {Key::kNumpad5, '5', 'u'},
      {Key::kNumpad6, '6', 'v'}, {Key::kNumpad7, '7', 'w'}, {Key::kNumpad8, '8', 'x'},
      {Key::kNumpad9, '9', 'y'},
      {Key::kNumpadDecimal, '.', 'n'}, {Key::kNumpadDivide, '/', 'o'},
      {Key::kNumpadMultiply, '*', 'j'}, {Key::kNumpadSubtract, '-', 'k'},
      {Key::kNumpadAdd, '+', 'l'}, {Key::kNumpadSeparator, ',', 'l'},
      {Key::kNumpadEnter, '\r', 'M'},
  };
  for (const auto& e : expected) {
    checkBytes(key(e.k), raw({uint8_t(e.num)}));
    checkBytes(key(e.k, Modifiers{}, deckpam()),
               raw({0x1b, uint8_t('O'), uint8_t(e.app)}));
  }
}

MINI_TEST(keypad_application_ss3_table) {
  const InputModes app = deckpam();
  checkBytes(key(Key::kNumpad0, Modifiers{}, app), esc("Op"));
  checkBytes(key(Key::kNumpad3, Modifiers{}, app), esc("Os"));
  checkBytes(key(Key::kNumpad5, Modifiers{}, app), esc("Ou"));
  checkBytes(key(Key::kNumpad9, Modifiers{}, app), esc("Oy"));
  checkBytes(key(Key::kNumpadDecimal, Modifiers{}, app), esc("On"));
  checkBytes(key(Key::kNumpadDivide, Modifiers{}, app), esc("Oo"));
  checkBytes(key(Key::kNumpadMultiply, Modifiers{}, app), esc("Oj"));
  checkBytes(key(Key::kNumpadSubtract, Modifiers{}, app), esc("Ok"));
  checkBytes(key(Key::kNumpadAdd, Modifiers{}, app), esc("Ol"));
  checkBytes(key(Key::kNumpadSeparator, Modifiers{}, app), esc("Ol"));
  checkBytes(key(Key::kNumpadEnter, Modifiers{}, app), esc("OM"));
}

MINI_TEST(keypad_numeric_ascii) {
  checkBytes(key(Key::kNumpad5), raw({'5'}));
  checkBytes(key(Key::kNumpad0), raw({'0'}));
  checkBytes(key(Key::kNumpadDecimal), raw({'.'}));
  checkBytes(key(Key::kNumpadDivide), raw({'/'}));
  checkBytes(key(Key::kNumpadMultiply), raw({'*'}));
  checkBytes(key(Key::kNumpadSubtract), raw({'-'}));
  checkBytes(key(Key::kNumpadAdd), raw({'+'}));
  checkBytes(key(Key::kNumpadSeparator), raw({','}));
  checkBytes(key(Key::kNumpadEnter), raw({0x0d}));
}

MINI_TEST(keypad_with_modifiers_degrades) {
  // Modified keypad keys degrade to the text path (no SS3 spelling).
  checkBytes(key(Key::kNumpad5, mShift(), deckpam()), raw({'5'}));
  checkBytes(key(Key::kNumpad5, mAlt(), deckpam()), raw({0x1b, '5'}));
  checkBytes(key(Key::kNumpad5, mShift()), raw({'5'}));
  // ctrl on keypad digits has no safe legacy encoding: empty.
  checkBytes(key(Key::kNumpad5, mCtrl(), deckpam()), Bytes{});
  checkBytes(key(Key::kNumpad0, mCtrl()), Bytes{});
  // Modified keypad Enter behaves as the main Enter key.
  checkBytes(key(Key::kNumpadEnter, mCtrl()), raw({0x0a}));
  checkBytes(key(Key::kNumpadEnter, mAlt()), raw({0x1b, 0x0d}));
}

MINI_TEST(modify_other_keys_level1) {
  const InputModes l1 = level(ModifyLevel::kModifyOtherKeys1);
  checkBytes(key(Key::kEnter, mCtrl(), l1), esc("[27;5;13~"));
  checkBytes(key(Key::kTab, mAlt(), l1), esc("[27;3;9~"));
  checkBytes(key(Key::kEscape, mCtrl(), l1), esc("[27;5;27~"));
  checkBytes(key(Key::kNumpadEnter, mCtrl(), l1), esc("[27;5;13~"));
  // Shift-only combos do NOT use the 27-form at level 1.
  checkBytes(key(Key::kEnter, mShift(), l1), raw({0x0d}));
  checkBytes(key(Key::kTab, mShift(), l1), esc("[Z"));
  // Space keeps the legacy spellings at level 1.
  checkBytes(key(Key::kSpace, mCtrl(), l1), raw({0x00}));
}

MINI_TEST(modify_other_keys_level2) {
  const InputModes l2 = level(ModifyLevel::kModifyOtherKeys2);
  checkBytes(key(Key::kTab, mShift(), l2), esc("[27;2;9~"));
  checkBytes(key(Key::kSpace, mCtrl(), l2), esc("[27;5;32~"));
  checkBytes(key(Key::kEnter, mShift(), l2), esc("[27;2;13~"));
  checkBytes(key(Key::kBackspace, mCtrlAlt(), l2), esc("[27;7;127~"));
  // Unmodified keys stay legacy; function keys keep parameterized forms.
  checkBytes(key(Key::kEnter, Modifiers{}, l2), raw({0x0d}));
  checkBytes(key(Key::kUp, mShift(), l2), esc("[1;2A"));
  checkBytes(key(Key::kF5, mShift(), l2), esc("[15;2~"));
}

MINI_TEST(kitty_protocol_modified_keys) {
  const InputModes kitty = level(ModifyLevel::kKittyProtocol);
  checkBytes(key(Key::kEnter, mCtrl(), kitty), esc("[13;6u"));   // 5+1
  checkBytes(key(Key::kTab, mShift(), kitty), esc("[9;3u"));     // 2+1
  checkBytes(key(Key::kSpace, mAlt(), kitty), esc("[32;4u"));    // 3+1
  checkBytes(key(Key::kUp, mCtrl(), kitty), esc("[1;6u"));       // fn=1
  checkBytes(key(Key::kF5, mShift(), kitty), esc("[17;3u"));     // fn=17
  checkBytes(key(Key::kNumpadEnter, mCtrl(), kitty), esc("[13;6u"));
}

MINI_TEST(kitty_protocol_unmodified_stays_legacy) {
  InputModes kitty = level(ModifyLevel::kKittyProtocol);
  checkBytes(key(Key::kEnter, Modifiers{}, kitty), raw({0x0d}));
  checkBytes(key(Key::kUp, Modifiers{}, kitty), esc("[A"));
  checkBytes(key(Key::kF5, Modifiers{}, kitty), esc("[15~"));
  kitty.applicationCursorKeys = true;
  checkBytes(key(Key::kUp, Modifiers{}, kitty), esc("OA"));
  kitty.applicationKeypad = true;
  checkBytes(key(Key::kNumpad5, Modifiers{}, kitty), esc("Ou"));
}

MINI_TEST(kitty_numpad_text_keys) {
  const InputModes kitty = level(ModifyLevel::kKittyProtocol);
  checkBytes(key(Key::kNumpad5, mCtrl(), kitty), esc("[53;6u"));
  checkBytes(key(Key::kNumpadAdd, mShift(), kitty), esc("[43;3u"));
}

MINI_TEST(paste_strips_control_and_escape) {
  // ESC must never survive into the PTY (sequence-injection defense).
  checkBytes(encodePasteUtf8("a\x1b[2Jb", 6, false), str("a[2Jb"));
  checkBytes(encodePasteUtf8("x\x07y", 3, false), str("xy"));
  checkBytes(encodePasteUtf8("x\x7fy", 3, false), str("xy"));
  checkBytes(encodePasteUtf8("x\x00\x1c\x1fy", 5, false), str("xy"));
}

MINI_TEST(paste_keeps_crlf_tab) {
  checkBytes(encodePasteUtf8("a\r\n\tb", 5, false), str("a\r\n\tb"));
  checkBytes(encodePasteUtf8("\r\n", 2, false), str("\r\n"));
}

MINI_TEST(paste_bracketed_wrap) {
  const Bytes wrapped = encodePasteUtf8("hi", 2, true);
  checkBytes(wrapped, raw({0x1b, '[', '2', '0', '0', '~', 'h', 'i', 0x1b, '[', '2', '0', '1', '~'}));
  checkBytes(encodePasteUtf8("", 0, true),
             raw({0x1b, '[', '2', '0', '0', '~', 0x1b, '[', '2', '0', '1', '~'}));
  checkBytes(encodePasteUtf8("", 0, false), Bytes{});
}

MINI_TEST(paste_utf8_truncation) {
  const char part[] = {'h', char(0xE4), char(0xB8)};  // truncated 3-byte seq
  checkBytes(encodePasteUtf8(part, 3, false), str("h"));
  const char full[] = {'h', char(0xE4), char(0xB8), char(0xAD)};
  checkBytes(encodePasteUtf8(full, 4, false), raw({'h', 0xE4, 0xB8, 0xAD}));
  const char tail[] = {'o', 'k', char(0xC3)};        // truncated 2-byte seq
  checkBytes(encodePasteUtf8(tail, 3, false), str("ok"));
}

MINI_TEST(utf8_valid_prefix_basics) {
  CHECK_EQ(utf8ValidPrefix("abc", 3), size_t(3));
  CHECK_EQ(utf8ValidPrefix("\xC3\xA9", 2), size_t(2));              // é
  CHECK_EQ(utf8ValidPrefix("\xE4\xB8\xAD", 3), size_t(3));          // 中
  CHECK_EQ(utf8ValidPrefix("\xF0\x9F\x98\x80", 4), size_t(4));      // 😀
  CHECK_EQ(utf8ValidPrefix("a\xE4\xB8", 3), size_t(1));             // truncated
  CHECK_EQ(utf8ValidPrefix("\xE4\xB8", 2), size_t(0));              // truncated
  CHECK_EQ(utf8ValidPrefix("ab\xFF", 3), size_t(2));                // bad lead
  CHECK_EQ(utf8ValidPrefix("ab\x80", 3), size_t(2));                // stray cont.
  CHECK_EQ(utf8ValidPrefix("", 0), size_t(0));
  CHECK_EQ(utf8ValidPrefix(nullptr, 0), size_t(0));
}

MINI_TEST(utf8_valid_prefix_rejections) {
  CHECK_EQ(utf8ValidPrefix("\xC0\xAF", 2), size_t(0));              // overlong
  CHECK_EQ(utf8ValidPrefix("\xE0\x80\x80", 3), size_t(0));          // overlong
  CHECK_EQ(utf8ValidPrefix("\xF0\x80\x80\x80", 4), size_t(0));      // overlong
  CHECK_EQ(utf8ValidPrefix("\xED\xA0\x80", 3), size_t(0));          // surrogate
  CHECK_EQ(utf8ValidPrefix("\xF4\x90\x80\x80", 4), size_t(0));      // > U+10FFFF
  CHECK_EQ(utf8ValidPrefix("ok\xED\x9F\xBF", 5), size_t(5));        // U+D7FF ok
  CHECK_EQ(utf8ValidPrefix("\xE0\xA0\x80", 3), size_t(3));          // U+0800 ok
  CHECK_EQ(utf8ValidPrefix("\xF4\x8F\xBF\xBF", 4), size_t(4));      // U+10FFFF ok
}

MINI_TEST(focus_events) {
  checkBytes(encodeFocus(true), esc("[I"));
  checkBytes(encodeFocus(false), esc("[O"));
}

MINI_TEST(modifier_param_values) {
  CHECK_EQ(modifierParam(Modifiers{}), 1);
  CHECK_EQ(modifierParam(mShift()), 2);
  CHECK_EQ(modifierParam(mAlt()), 3);
  CHECK_EQ(modifierParam(mCtrl()), 5);
  CHECK_EQ(modifierParam(mMeta()), 9);
  Modifiers all;
  all.shift = all.ctrl = all.alt = all.meta = true;
  CHECK_EQ(modifierParam(all), 16);
}

MINI_TEST(none_key_not_encodable) {
  checkBytes(encodeKey(Key::kNone, Modifiers{}, plain()), Bytes{});
}

MINI_TEST(every_key_encodes_without_modifiers) {
  // Total-function sanity: all 44 real keys produce a non-empty sequence in
  // the default modes (kNumpad keys only fail with ctrl — see the keypad
  // degrade test), so an IME-side mapping can never silently vanish.
  for (int v = int(Key::kUp); v <= int(Key::kNumpadSeparator); ++v) {
    const Bytes b = encodeKey(Key(uint16_t(v)), Modifiers{}, InputModes{});
    CHECK(!b.empty());
  }
}

MINI_TEST(modify_levels_keep_unmodified_keys_legacy) {
  const ModifyLevel levels[] = {
      ModifyLevel::kNone, ModifyLevel::kModifyOtherKeys1,
      ModifyLevel::kModifyOtherKeys2, ModifyLevel::kKittyProtocol,
  };
  for (ModifyLevel l : levels) {
    InputModes i;
    i.modifyLevel = l;
    checkBytes(encodeKey(Key::kEnter, Modifiers{}, i), raw({0x0d}));
    checkBytes(encodeKey(Key::kUp, Modifiers{}, i), esc("[A"));
    checkBytes(encodeKey(Key::kF1, Modifiers{}, i), esc("OP"));
    checkBytes(encodeKey(Key::kNumpad5, Modifiers{}, i), raw({'5'}));
  }
}

MINI_TEST(paste_midstream_invalid_utf8_cuts) {
  // Prefix semantics: everything from the first invalid byte onward is
  // dropped (nothing after a broken sequence is ever trusted).
  const char bad[] = {'a', char(0xFF), 'b'};
  checkBytes(encodePasteUtf8(bad, 3, false), str("a"));
  const char bad2[] = {'a', 'b', char(0xC3), char(0x28)};
  checkBytes(encodePasteUtf8(bad2, 4, false), str("ab"));
}

MINI_TEST(paste_bracketed_still_filters_payload) {
  const char in[] = {'\x1b', '[', '2', 'J', 'x'};  // attempted sequence inject
  checkBytes(encodePasteUtf8(in, 5, true),
             raw({0x1b, '[', '2', '0', '0', '~', '[', '2', 'J', 'x',
                  0x1b, '[', '2', '0', '1', '~'}));
}
