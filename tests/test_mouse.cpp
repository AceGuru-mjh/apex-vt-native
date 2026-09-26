// Mouse-encoding tests — byte-exact contract checks for vt_mouse: the four
// tracking modes × four wire encodings, the full gating matrix and the
// coordinate/modifier/wheel arithmetic.
#include "mini_test.h"

#include "../src/vt_mouse.h"

#include <cstdint>
#include <vector>

using namespace apex::vt::mouse;
using apex::vt::input::Modifiers;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes esc(const char* s) {
  Bytes b;
  b.push_back(0x1b);
  while (*s != '\0') b.push_back(uint8_t(*s++));
  return b;
}

Bytes raw(std::initializer_list<uint8_t> bs) { return Bytes(bs); }

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

MouseEvent ev(Type t, int button, int col, int row, Modifiers m = Modifiers{}) {
  MouseEvent e;
  e.type = t;
  e.button = button;
  e.col = col;
  e.row = row;
  e.mods = m;
  return e;
}

Modifiers mShift() { Modifiers m; m.shift = true; return m; }
Modifiers mCtrl() { Modifiers m; m.ctrl = true; return m; }
Modifiers mMeta() { Modifiers m; m.meta = true; return m; }
Modifiers mAlt() { Modifiers m; m.alt = true; return m; }

}  // namespace

MINI_TEST(sgr_press_and_release) {
  checkBytes(encode(ev(Type::kPress, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<0;3;7M"));
  checkBytes(encode(ev(Type::kPress, 1, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<1;3;7M"));
  checkBytes(encode(ev(Type::kPress, 2, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<2;3;7M"));
  // SGR release keeps the REAL button and lowers the final byte to 'm'.
  checkBytes(encode(ev(Type::kRelease, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<0;3;7m"));
  checkBytes(encode(ev(Type::kRelease, 2, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<2;3;7m"));
}

MINI_TEST(sgr_modifiers_and_large_coords) {
  // shift=4, meta=8, ctrl=16; alt has no mouse bit.
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mShift()), Mode::kNormal, Encoding::kSgr),
             esc("[<4;3;7M"));
  checkBytes(encode(ev(Type::kPress, 1, 3, 7, mCtrl()), Mode::kNormal, Encoding::kSgr),
             esc("[<17;3;7M"));
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mMeta()), Mode::kNormal, Encoding::kSgr),
             esc("[<8;3;7M"));
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mAlt()), Mode::kNormal, Encoding::kSgr),
             esc("[<0;3;7M"));
  checkBytes(encode(ev(Type::kRelease, 1, 3, 7, mShift()), Mode::kNormal, Encoding::kSgr),
             esc("[<5;3;7m"));
  // SGR coordinates are unbounded decimal.
  checkBytes(encode(ev(Type::kPress, 0, 500, 900), Mode::kAny, Encoding::kSgr),
             esc("[<0;500;900M"));
}

MINI_TEST(x11_press_release_and_modifiers) {
  // (col,row)=(3,7): Cb=32+0, Cx=32+2=34 ('"'), Cy=32+6=38 ('&').
  checkBytes(encode(ev(Type::kPress, 0, 3, 7), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x20, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kPress, 2, 3, 7), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x22, 0x22, 0x26}));
  // Legacy release: button bits forced to 3 (Cb = 32+3 = 0x23).
  checkBytes(encode(ev(Type::kRelease, 0, 3, 7), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x23, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kRelease, 2, 3, 7), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x23, 0x22, 0x26}));
  // Modifier bits ride along: shift adds 4.
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mShift()), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x24, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kRelease, 0, 3, 7, mShift()), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x27, 0x22, 0x26}));
  // Top-left corner: all three bytes at their 32 floor.
  checkBytes(encode(ev(Type::kPress, 0, 1, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x20, 0x20, 0x20}));
}

MINI_TEST(x11_coordinate_bounds) {
  // col 223 → Cx = 254 (last encodable byte); col 224 → not representable.
  checkBytes(encode(ev(Type::kPress, 0, 223, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x20, 0xfe, 0x20}));
  CHECK(encode(ev(Type::kPress, 0, 224, 1), Mode::kNormal, Encoding::kX11).empty());
  CHECK(encode(ev(Type::kPress, 0, 1, 224), Mode::kNormal, Encoding::kX11).empty());
  // Coordinates below 1 clamp to the top-left corner.
  checkBytes(encode(ev(Type::kPress, 0, -5, 0), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x20, 0x20, 0x20}));
}

MINI_TEST(x11_and_sgr_wheel) {
  // Wheel rides on button bits 64+idx: X11 Cb = 32+64 = 0x60 (up) … 0x63.
  checkBytes(encode(ev(Type::kWheelUp, 0, 1, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x60, 0x20, 0x20}));
  checkBytes(encode(ev(Type::kWheelDown, 0, 1, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x61, 0x20, 0x20}));
  checkBytes(encode(ev(Type::kWheelLeft, 0, 1, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x62, 0x20, 0x20}));
  checkBytes(encode(ev(Type::kWheelRight, 0, 1, 1), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x63, 0x20, 0x20}));
  checkBytes(encode(ev(Type::kWheelUp, 0, 1, 1, mShift()), Mode::kNormal, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x64, 0x20, 0x20}));
  // SGR: 64=up 65=down 66=left 67=right, modifiers OR'd in.
  checkBytes(encode(ev(Type::kWheelUp, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<64;3;7M"));
  checkBytes(encode(ev(Type::kWheelDown, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<65;3;7M"));
  checkBytes(encode(ev(Type::kWheelLeft, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<66;3;7M"));
  checkBytes(encode(ev(Type::kWheelRight, 0, 3, 7), Mode::kNormal, Encoding::kSgr),
             esc("[<67;3;7M"));
  checkBytes(encode(ev(Type::kWheelUp, 0, 3, 7, mCtrl()), Mode::kNormal, Encoding::kSgr),
             esc("[<80;3;7M"));
}

MINI_TEST(utf8_encoding_coordinates) {
  // Cb=32 stays one byte; Cx=281 → U+0119 (C4 99); Cy=291 → U+0123 (C4 A3).
  checkBytes(encode(ev(Type::kPress, 0, 250, 260), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x20, 0xc4, 0x99, 0xc4, 0xa3}));
  checkBytes(encode(ev(Type::kPress, 0, 1, 1), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x20, 0x20, 0x20}));
  // Wheel Cb 96 = '`' is still pure ASCII.
  checkBytes(encode(ev(Type::kWheelUp, 0, 2, 2), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x60, 0x21, 0x21}));
  // Limit: Cx = 2015 = U+07DF (DF 9F); col 1985 exceeds it → empty.
  checkBytes(encode(ev(Type::kPress, 0, 1984, 1), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x20, 0xdf, 0x9f, 0x20}));
  CHECK(encode(ev(Type::kPress, 0, 1985, 1), Mode::kAny, Encoding::kUtf8).empty());
}

MINI_TEST(urxvt_extended_encoding) {
  // ESC [ Cb ; col ; row M with 1-based decimal coords; Cb as in X11.
  checkBytes(encode(ev(Type::kPress, 0, 3, 7), Mode::kNormal, Encoding::kUrxvt),
             esc("[32;3;7M"));
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mShift()), Mode::kNormal, Encoding::kUrxvt),
             esc("[36;3;7M"));
  checkBytes(encode(ev(Type::kRelease, 0, 3, 7), Mode::kNormal, Encoding::kUrxvt),
             esc("[35;3;7M"));
  checkBytes(encode(ev(Type::kMotion, 0, 3, 7), Mode::kAny, Encoding::kUrxvt),
             esc("[64;3;7M"));
  checkBytes(encode(ev(Type::kMotion, -1, 3, 7), Mode::kAny, Encoding::kUrxvt),
             esc("[67;3;7M"));
  checkBytes(encode(ev(Type::kWheelUp, 0, 3, 7), Mode::kNormal, Encoding::kUrxvt),
             esc("[96;3;7M"));
}

MINI_TEST(x10_mode_semantics) {
  // X10 reports plain presses only, with modifiers stripped.
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mShift()), Mode::kX10, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x20, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kPress, 1, 3, 7, mCtrl()), Mode::kX10, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x21, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kPress, 0, 3, 7, mShift()), Mode::kX10, Encoding::kSgr),
             esc("[<0;3;7M"));
  CHECK(encode(ev(Type::kRelease, 0, 3, 7), Mode::kX10, Encoding::kX11).empty());
  CHECK(encode(ev(Type::kMotion, 0, 3, 7), Mode::kX10, Encoding::kX11).empty());
  CHECK(encode(ev(Type::kWheelUp, 0, 3, 7), Mode::kX10, Encoding::kX11).empty());
  CHECK(encode(ev(Type::kPress, 3, 3, 7), Mode::kX10, Encoding::kX11).empty());
}

MINI_TEST(sgr_motion_forms) {
  // Motion flag = 32; no-button motion uses button bits 3 → Cb 35.
  checkBytes(encode(ev(Type::kMotion, 0, 3, 7), Mode::kAny, Encoding::kSgr),
             esc("[<32;3;7M"));
  checkBytes(encode(ev(Type::kMotion, 1, 3, 7), Mode::kAny, Encoding::kSgr),
             esc("[<33;3;7M"));
  checkBytes(encode(ev(Type::kMotion, -1, 3, 7), Mode::kAny, Encoding::kSgr),
             esc("[<35;3;7M"));
  checkBytes(encode(ev(Type::kMotion, 0, 3, 7, mShift()), Mode::kAny, Encoding::kSgr),
             esc("[<36;3;7M"));
  // X11 motion: Cb = 32 + button + 32 (button bits 3 when none held).
  checkBytes(encode(ev(Type::kMotion, 1, 5, 5), Mode::kAny, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x41, 0x24, 0x24}));
  checkBytes(encode(ev(Type::kMotion, -1, 5, 5), Mode::kAny, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x43, 0x24, 0x24}));
}

MINI_TEST(gating_matrix_all_modes_all_types) {
  // 5 modes × 7 event types (button 0 held where applicable).
  const Mode modes[] = {Mode::kOff, Mode::kX10, Mode::kNormal, Mode::kButton, Mode::kAny};
  const Type types[] = {Type::kPress,  Type::kRelease, Type::kMotion,
                        Type::kWheelUp, Type::kWheelDown, Type::kWheelLeft,
                        Type::kWheelRight};
  const bool expect[5][7] = {
      // kOff: nothing
      {false, false, false, false, false, false, false},
      // kX10: press only (no release / motion / wheel)
      {true, false, false, false, false, false, false},
      // kNormal: press + release + wheel, no motion
      {true, true, false, true, true, true, true},
      // kButton: + motion while held
      {true, true, true, true, true, true, true},
      // kAny: everything
      {true, true, true, true, true, true, true},
  };
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 7; ++j) {
      const Bytes b = encode(ev(types[j], 0, 3, 7), modes[i], Encoding::kSgr);
      CHECK_EQ(b.empty(), !expect[i][j]);
    }
  }
  // No-button motion is only reported in kAny (mode 1003).
  const bool expectNoButton[5] = {false, false, false, false, true};
  for (int i = 0; i < 5; ++i) {
    const Bytes b = encode(ev(Type::kMotion, -1, 3, 7), modes[i], Encoding::kSgr);
    CHECK_EQ(b.empty(), !expectNoButton[i]);
  }
}

MINI_TEST(invalid_buttons_rejected) {
  CHECK(encode(ev(Type::kPress, 3, 3, 7), Mode::kAny, Encoding::kSgr).empty());
  CHECK(encode(ev(Type::kPress, -1, 3, 7), Mode::kAny, Encoding::kSgr).empty());
  CHECK(encode(ev(Type::kRelease, 5, 3, 7), Mode::kNormal, Encoding::kSgr).empty());
  CHECK(encode(ev(Type::kMotion, 3, 3, 7), Mode::kAny, Encoding::kSgr).empty());
  CHECK(encode(ev(Type::kMotion, -2, 3, 7), Mode::kAny, Encoding::kSgr).empty());
}

MINI_TEST(x11_motion_with_modifiers) {
  // Cb = 32 + button + mods + 32 = 32+0+4+32 = 68 = 'D'.
  checkBytes(encode(ev(Type::kMotion, 0, 5, 5, mShift()), Mode::kAny, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x44, 0x24, 0x24}));
  checkBytes(encode(ev(Type::kMotion, 2, 5, 5, mCtrl()), Mode::kAny, Encoding::kX11),
             raw({0x1b, '[', 'M', 0x52, 0x24, 0x24}));  // 32+2+16+32 = 82
}

MINI_TEST(mode_and_encoding_are_orthogonal) {
  // X10 presses serialize in every requested encoding (mods stripped).
  const MouseEvent p = ev(Type::kPress, 1, 3, 7, mCtrl());
  checkBytes(encode(p, Mode::kX10, Encoding::kSgr), esc("[<1;3;7M"));
  checkBytes(encode(p, Mode::kX10, Encoding::kUrxvt), esc("[33;3;7M"));
  checkBytes(encode(p, Mode::kX10, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x21, 0x22, 0x26}));
}

MINI_TEST(utf8_ascii_values_match_x11) {
  // All values below 128 serialize byte-identically to the X11 form.
  checkBytes(encode(ev(Type::kRelease, 2, 3, 7), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x23, 0x22, 0x26}));
  checkBytes(encode(ev(Type::kMotion, 0, 5, 5), Mode::kAny, Encoding::kUtf8),
             raw({0x1b, '[', 'M', 0x40, 0x24, 0x24}));
}
