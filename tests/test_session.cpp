// test_session — session codec tests.
//
// Coverage:
//   * full / empty / minimal (1×1) round-trips with field-by-field deep
//     compare (styles, wide lead+trail cells, combining marks, scrollback,
//     CJK title, mouse modes, SGR stack)
//   * CRC-32 known vectors + cross-check against a bit-by-bit reference
//   * byte determinism of repeated save() calls
//   * payload size sanity (no accidental bloat)
//   * corruption matrix: every injected fault MUST be rejected by load()
//     without crashing and without touching the output state
//
// Corruption cases patch the buffer at offsets documented by the wire
// format in vt_session.cpp; fixCrc() re-seals the header so each case
// exercises its intended structural check instead of failing at the CRC.
#include "mini_test.h"

#include "../src/vt_session.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace apex::vt;
using namespace apex::vt::session;

namespace {

// ─── wire-format mirrors (test side) ──────────────────────────────────────
constexpr size_t kHeaderSize = 14;    // magic u32 | version u16 | payloadLen u32 | crc u32
constexpr size_t kSectionCount = 13;  // dims … sgr, fixed order
constexpr int kRows = 24, kCols = 80; // fixture: classic 80×24 screen
constexpr size_t kStyleRec = 12;      // style record: fg 5 | bg 5 | attr u16
constexpr uint16_t kFixtureStyles = 4;  // styles in the fixture
// scrollback 行 0 的 mark0 偏移：lineCount + cellCount + cols 个 cell + markCount
constexpr size_t kSbLine0Mark = 4 + 4 + size_t(kCols) * 8 + 4;

// Section body offsets (order fixed by vt_session.cpp).
enum SecIdx {
  kSiDims = 0, kSiCursor, kSiModes, kSiStyles, kSiGrid, kSiMarks, kSiScrollback,
  kSiTabs, kSiTitle, kSiCharsets, kSiMouse, kSiMisc, kSiSgr,
};

uint32_t rdU32(const std::vector<uint8_t>& b, size_t off) {
  return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) |
         (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24);
}

void wrU8(std::vector<uint8_t>* b, size_t off, uint8_t v) { (*b)[off] = v; }
void wrU16(std::vector<uint8_t>* b, size_t off, uint16_t v) {
  (*b)[off] = uint8_t(v);
  (*b)[off + 1] = uint8_t(v >> 8);
}
void wrU32(std::vector<uint8_t>* b, size_t off, uint32_t v) {
  (*b)[off] = uint8_t(v);
  (*b)[off + 1] = uint8_t(v >> 8);
  (*b)[off + 2] = uint8_t(v >> 16);
  (*b)[off + 3] = uint8_t(v >> 24);
}
void wrI32(std::vector<uint8_t>* b, size_t off, int32_t v) {
  uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  wrU32(b, off, u);
}

// Recompute the header CRC after an in-payload patch, so the corruption
// reaches the intended structural check instead of the CRC gate.
void fixCrc(std::vector<uint8_t>* b) {
  const uint32_t plen = rdU32(*b, 6);
  wrU32(b, 10, crc32(b->data() + kHeaderSize, plen));
}

struct SecInfo {
  size_t off = 0;  // body start (past the u32 length prefix)
  size_t len = 0;  // body length
};

// Walks the 13 sections; requires the payload to be exactly consumed
// (the same format invariant load() enforces).
bool walkSections(const std::vector<uint8_t>& b, std::vector<SecInfo>* out) {
  if (b.size() < kHeaderSize) return false;
  const uint32_t plen = rdU32(b, 6);
  if (plen != b.size() - kHeaderSize) return false;
  size_t pos = kHeaderSize;
  for (size_t i = 0; i < kSectionCount; ++i) {
    if (pos + 4 > b.size()) return false;
    const uint32_t len = rdU32(b, pos);
    pos += 4;
    if (pos + len > b.size()) return false;
    out->push_back(SecInfo{pos, len});
    pos += len;
  }
  return pos == b.size();
}

// ─── fixture builders ─────────────────────────────────────────────────────
Style styleBoldRed() {
  Style s;
  s.fg.kind = kColorIndexed;
  s.fg.index = 9;
  s.attr = kAttrBold;
  return s;
}
Style styleRgbUnderline() {
  Style s;
  s.fg.kind = kColorRgb;
  s.fg.r = 0x12;
  s.fg.g = 0x34;
  s.fg.b = 0x56;
  s.bg.kind = kColorRgb;
  s.bg.r = 0xAB;
  s.bg.g = 0xCD;
  s.bg.b = 0xEF;
  s.attr = uint16_t(kUlDouble << kUlShift) | kAttrItalic | kAttrDim;
  return s;
}
Style styleInverse() {
  Style s;
  s.fg.kind = kColorIndexed;
  s.fg.index = 3;
  s.bg.kind = kColorIndexed;
  s.bg.index = 252;
  s.attr = kAttrInverse | kAttrStrike | uint16_t(kUlCurly << kUlShift);
  return s;
}

// A state exercising every serialized feature: 24×80 grid with rotating
// styles + a wide lead/trail pair, visible marks, 100 scrollback lines with
// periodic marks, CJK+emoji title, mouse mode 3/SGR encoding, SGR stack.
SessionState makeFullState() {
  SessionState s;
  s.rows = kRows;
  s.cols = kCols;
  s.cursorRow = 5;
  s.cursorCol = 7;
  s.wrapPending = true;
  s.cursorVisible = false;
  s.applicationCursor = true;
  s.bracketedPaste = true;
  s.alternateScreen = true;
  s.reverseVideo = true;
  s.originMode = true;
  s.cursorShape = CursorShape::kBlock;
  s.scrollTop = 2;
  s.scrollBottom = 21;
  s.savedCursorRow = 9;
  s.savedCursorCol = 3;
  s.savedWrapPending = true;
  s.savedOriginMode = false;

  s.styles = {kDefaultStyle, styleBoldRed(), styleRgbUnderline(), styleInverse()};

  s.grid.assign(size_t(kRows) * kCols, Cell{});
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      Cell cell{};
      cell.cp = uint32_t('a' + ((r * kCols + c) % 26));
      cell.style = uint16_t((r + c) % 4);
      s.grid[size_t(r) * kCols + c] = cell;
    }
  }
  // Wide char (U+4E2D): lead carries the code point, trail has cp == 0.
  s.grid[size_t(1) * kCols + 10] = Cell{uint32_t(U'中'), 1, kCellWideLead};
  s.grid[size_t(1) * kCols + 11] = Cell{0, 1, kCellWideTrail};

  s.gridMarks.push_back(Mark{2, 5, {0x0301, 0x0308}});
  s.gridMarks.push_back(Mark{3, 7, {0xFE0F}});
  s.gridMarks.push_back(Mark{20, 40, {0x0301, 0x0302, 0x0303, 0x0304}});

  s.scrollback.clear();
  for (int i = 0; i < 100; ++i) {
    SessionRow row;
    row.cells.assign(kCols, Cell{});
    const uint32_t cp = uint32_t('A' + (i % 26));
    const uint16_t st = uint16_t(i % 4);
    for (int c = 0; c < kCols; ++c) row.cells[c] = Cell{cp, st, 0};
    if (i % 10 == 0) row.marks.push_back(Mark{i, 3, {0x0301}});
    s.scrollback.push_back(std::move(row));
  }
  s.scrollbackLinesEver = 123456;

  s.tabStops.assign(kCols, 0);
  for (int c = 0; c < kCols; ++c) s.tabStops[c] = (c % 8 == 0) ? 1 : 0;

  s.title = "中文标题 – terminal ✅";

  s.g0DecGraphics = true;
  s.g1DecGraphics = false;
  s.shiftOutG1 = true;
  s.mouseMode = 3;      // any-event tracking
  s.mouseEncoding = 2;  // SGR
  s.lastPrintable = uint32_t(U'中');
  s.sgrStack = {styleBoldRed(), styleInverse()};
  return s;
}

// ─── comparison helpers ───────────────────────────────────────────────────
bool cellEq(const Cell& a, const Cell& b) {
  return a.cp == b.cp && a.style == b.style && a.flags == b.flags;
}
bool markEq(const Mark& a, const Mark& b) {
  return a.row == b.row && a.col == b.col && a.cps == b.cps;
}

// Deep field-by-field comparison — reports every mismatch. All vector
// loops are bounded by BOTH sides so a size mismatch reports as a failed
// CHECK_EQ instead of an out-of-bounds read.
void checkSame(const SessionState& a, const SessionState& b) {
  CHECK_EQ(a.rows, b.rows);
  CHECK_EQ(a.cols, b.cols);
  CHECK_EQ(a.cursorRow, b.cursorRow);
  CHECK_EQ(a.cursorCol, b.cursorCol);
  CHECK_EQ(a.wrapPending, b.wrapPending);
  CHECK_EQ(a.cursorVisible, b.cursorVisible);
  CHECK_EQ(a.applicationCursor, b.applicationCursor);
  CHECK_EQ(a.bracketedPaste, b.bracketedPaste);
  CHECK_EQ(a.alternateScreen, b.alternateScreen);
  CHECK_EQ(a.reverseVideo, b.reverseVideo);
  CHECK_EQ(a.originMode, b.originMode);
  CHECK(a.cursorShape == b.cursorShape);  // scoped enum — no toStr printer
  CHECK_EQ(a.scrollTop, b.scrollTop);
  CHECK_EQ(a.scrollBottom, b.scrollBottom);
  CHECK_EQ(a.savedCursorRow, b.savedCursorRow);
  CHECK_EQ(a.savedCursorCol, b.savedCursorCol);
  CHECK_EQ(a.savedWrapPending, b.savedWrapPending);
  CHECK_EQ(a.savedOriginMode, b.savedOriginMode);

  CHECK_EQ(a.styles.size(), b.styles.size());
  const size_t nStyles = a.styles.size() < b.styles.size() ? a.styles.size() : b.styles.size();
  for (size_t i = 0; i < nStyles; ++i) CHECK(a.styles[i] == b.styles[i]);

  CHECK_EQ(a.grid.size(), b.grid.size());
  const size_t nGrid = a.grid.size() < b.grid.size() ? a.grid.size() : b.grid.size();
  for (size_t i = 0; i < nGrid; ++i) CHECK(cellEq(a.grid[i], b.grid[i]));

  CHECK_EQ(a.gridMarks.size(), b.gridMarks.size());
  const size_t nMarks = a.gridMarks.size() < b.gridMarks.size() ? a.gridMarks.size() : b.gridMarks.size();
  for (size_t i = 0; i < nMarks; ++i) {
    CHECK(markEq(a.gridMarks[i], b.gridMarks[i]));
  }

  CHECK_EQ(a.scrollback.size(), b.scrollback.size());
  const size_t nRows = a.scrollback.size() < b.scrollback.size() ? a.scrollback.size() : b.scrollback.size();
  for (size_t i = 0; i < nRows; ++i) {
    const SessionRow& ra = a.scrollback[i];
    const SessionRow& rb = b.scrollback[i];
    CHECK_EQ(ra.cells.size(), rb.cells.size());
    const size_t nCells = ra.cells.size() < rb.cells.size() ? ra.cells.size() : rb.cells.size();
    for (size_t j = 0; j < nCells; ++j) CHECK(cellEq(ra.cells[j], rb.cells[j]));
    CHECK_EQ(ra.marks.size(), rb.marks.size());
    const size_t nRowMarks = ra.marks.size() < rb.marks.size() ? ra.marks.size() : rb.marks.size();
    for (size_t j = 0; j < nRowMarks; ++j) CHECK(markEq(ra.marks[j], rb.marks[j]));
  }

  CHECK_EQ(a.scrollbackLinesEver, b.scrollbackLinesEver);
  CHECK(a.tabStops == b.tabStops);
  CHECK_EQ(a.title, b.title);
  CHECK_EQ(a.g0DecGraphics, b.g0DecGraphics);
  CHECK_EQ(a.g1DecGraphics, b.g1DecGraphics);
  CHECK_EQ(a.shiftOutG1, b.shiftOutG1);
  CHECK_EQ(a.mouseMode, b.mouseMode);
  CHECK_EQ(a.mouseEncoding, b.mouseEncoding);
  CHECK_EQ(a.lastPrintable, b.lastPrintable);
  CHECK_EQ(a.sgrStack.size(), b.sgrStack.size());
  const size_t nSgr = a.sgrStack.size() < b.sgrStack.size() ? a.sgrStack.size() : b.sgrStack.size();
  for (size_t i = 0; i < nSgr; ++i) CHECK(a.sgrStack[i] == b.sgrStack[i]);
}

// Loads a corrupted variant: must be rejected, and a rejected load must
// leave the output state untouched.
bool rejected(const std::vector<uint8_t>& buf) {
  SessionState out;
  out.rows = 77;  // sentinel
  const bool ok = load(buf.data(), buf.size(), &out);
  if (!ok) CHECK_EQ(out.rows, 77);  // 失败时 out 不变
  return !ok;
}

// Fresh saved fixture + walked sections (tests mutate the buffer).
struct Fixture {
  SessionState state;
  std::vector<uint8_t> buf;
  std::vector<SecInfo> secs;

  Fixture() {
    state = makeFullState();
    CHECK(save(state, &buf));
    CHECK(walkSections(buf, &secs));
    CHECK_EQ(secs.size(), kSectionCount);
  }
  size_t off(int idx) const { return secs[size_t(idx)].off; }
  size_t len(int idx) const { return secs[size_t(idx)].len; }
};

// One-shot corruption case: build a FRESH fixture, patch one field at
// (section, body offset), re-seal the CRC and assert the load is rejected.
// A fresh fixture per case keeps the injections independent.
void rejectU8(int sec, size_t at, uint8_t v) {
  Fixture fx;
  wrU8(&fx.buf, fx.off(sec) + at, v);
  fixCrc(&fx.buf);
  CHECK(rejected(fx.buf));
}
void rejectU16(int sec, size_t at, uint16_t v) {
  Fixture fx;
  wrU16(&fx.buf, fx.off(sec) + at, v);
  fixCrc(&fx.buf);
  CHECK(rejected(fx.buf));
}
void rejectU32(int sec, size_t at, uint32_t v) {
  Fixture fx;
  wrU32(&fx.buf, fx.off(sec) + at, v);
  fixCrc(&fx.buf);
  CHECK(rejected(fx.buf));
}
void rejectI32(int sec, size_t at, int32_t v) {
  Fixture fx;
  wrI32(&fx.buf, fx.off(sec) + at, v);
  fixCrc(&fx.buf);
  CHECK(rejected(fx.buf));
}

// Bit-by-bit reference CRC (independent of the table-driven one).
uint32_t crc32Ref(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
  }
  return c ^ 0xFFFFFFFFu;
}

}  // namespace

// ─── CRC-32 ───────────────────────────────────────────────────────────────
MINI_TEST(crc32_known_vectors) {
  CHECK_EQ(crc32(nullptr, 0), uint32_t(0));  // 空 → 0
  const uint8_t* v = reinterpret_cast<const uint8_t*>("123456789");
  CHECK_EQ(crc32(v, 9), uint32_t(0xCBF43926));  // IEEE 检验值
  const uint8_t* a = reinterpret_cast<const uint8_t*>("a");
  CHECK_EQ(crc32(a, 1), uint32_t(0xE8B7BE43));  // zlib 参考值
  // 与逐位参考实现对拍。
  const char* inputs[] = {"", "a", "abc", "hello world", "123456789", "中文"};
  for (const char* in : inputs) {
    const size_t n = std::strlen(in);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in);
    CHECK_EQ(crc32(p, n), crc32Ref(p, n));
  }
}

// ─── round-trips ──────────────────────────────────────────────────────────
MINI_TEST(save_load_full_roundtrip) {
  const SessionState orig = makeFullState();
  std::vector<uint8_t> buf;
  CHECK(save(orig, &buf));
  CHECK(buf.size() > kHeaderSize);
  CHECK_EQ(rdU32(buf, 0), kMagic);
  CHECK_EQ(rdU32(buf, 6), uint32_t(buf.size() - kHeaderSize));

  SessionState loaded;
  CHECK(load(buf.data(), buf.size(), &loaded));
  checkSame(orig, loaded);

  // Spot checks (deep compare already covers everything; these localize
  // failures to a feature).
  CHECK_EQ(loaded.grid[size_t(1) * kCols + 10].cp, uint32_t(U'中'));
  CHECK_EQ(loaded.grid[size_t(1) * kCols + 10].flags, uint16_t(kCellWideLead));
  CHECK_EQ(loaded.grid[size_t(1) * kCols + 11].flags, uint16_t(kCellWideTrail));
  CHECK_EQ(loaded.grid[size_t(1) * kCols + 11].cp, uint32_t(0));
  CHECK_EQ(loaded.gridMarks[0].cps.size(), size_t(2));
  CHECK_EQ(loaded.gridMarks[0].cps[1], uint32_t(0x0308));
  CHECK_EQ(loaded.scrollback[0].marks.size(), size_t(1));
  CHECK_EQ(loaded.scrollback[7].marks.size(), size_t(0));
  CHECK_EQ(loaded.scrollback[0].marks[0].row, int32_t(0));
  CHECK_EQ(loaded.scrollback[99].cells[79].cp, uint32_t('A' + 99 % 26));
  CHECK_EQ(loaded.title, std::string("中文标题 – terminal ✅"));
  CHECK_EQ(loaded.mouseMode, uint8_t(3));
  CHECK_EQ(loaded.mouseEncoding, uint8_t(2));
  CHECK_EQ(loaded.sgrStack.size(), size_t(2));
  CHECK_EQ(loaded.lastPrintable, uint32_t(U'中'));
}

MINI_TEST(save_load_empty_state) {
  SessionState empty;  // rows=0, cols=0 — canonical empty state
  std::vector<uint8_t> buf;
  CHECK(save(empty, &buf));
  SessionState loaded;
  loaded.rows = 3;  // 预填非默认，验证成功时整体覆盖
  loaded.title = "junk";
  CHECK(load(buf.data(), buf.size(), &loaded));
  CHECK_EQ(loaded.rows, 0);
  CHECK_EQ(loaded.cols, 0);
  CHECK(loaded.grid.empty());
  CHECK(loaded.scrollback.empty());
  CHECK(loaded.gridMarks.empty());
  CHECK(loaded.styles.empty());
  CHECK(loaded.tabStops.empty());
  CHECK(loaded.title.empty());
  checkSame(empty, loaded);
}

MINI_TEST(save_load_minimal_1x1) {
  SessionState s;
  s.rows = 1;
  s.cols = 1;
  s.cursorRow = 0;
  s.cursorCol = 0;
  s.cursorShape = CursorShape::kUnderline;
  s.styles = {kDefaultStyle};
  s.grid = {Cell{uint32_t('x'), 0, 0}};
  s.tabStops = {1};
  std::vector<uint8_t> buf;
  CHECK(save(s, &buf));
  SessionState loaded;
  CHECK(load(buf.data(), buf.size(), &loaded));
  checkSame(s, loaded);
  CHECK_EQ(loaded.grid[0].cp, uint32_t('x'));
}

MINI_TEST(save_is_deterministic) {
  const SessionState s = makeFullState();
  std::vector<uint8_t> a, b;
  CHECK(save(s, &a));
  CHECK(save(s, &b));
  CHECK_EQ(a.size(), b.size());
  CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0);
  // 重复 save 到已有内容的 out 也字节一致（覆盖语义）。
  std::vector<uint8_t> c(64, 0xEE);
  CHECK(save(s, &c));
  CHECK_EQ(a.size(), c.size());
  CHECK(a.size() == c.size() && std::memcmp(a.data(), c.data(), a.size()) == 0);
}

MINI_TEST(payload_size_is_bounded) {
  SessionState s;
  s.rows = 24;
  s.cols = 80;
  s.styles = {kDefaultStyle};
  s.grid.assign(size_t(24) * 80, Cell{uint32_t(' '), 0, 0});
  s.tabStops.assign(80, 1);
  for (int i = 0; i < 1000; ++i) {  // 千行回滚
    SessionRow row;
    row.cells.assign(80, Cell{uint32_t('x'), 0, 0});
    s.scrollback.push_back(std::move(row));
  }
  std::vector<uint8_t> buf;
  CHECK(save(s, &buf));
  const uint32_t plen = rdU32(buf, 6);
  // 每单元 8 B + 每行 12 B 开销；粗上限 24 B/cell/行 防意外膨胀。
  CHECK(plen < 24u * 80u * 1000u);
  CHECK(buf.size() == kHeaderSize + plen);
}

MINI_TEST(api_null_args_rejected) {
  SessionState s;
  CHECK(!save(s, nullptr));
  CHECK(!load(nullptr, 16, &s));
  std::vector<uint8_t> b;
  CHECK(save(s, &b));
  CHECK(!load(b.data(), b.size(), nullptr));
}

// ─── corruption matrix ────────────────────────────────────────────────────
MINI_TEST(load_rejects_bad_magic) {
  Fixture fx;
  fx.buf[0] ^= 0xFF;  // magic 改 1 字节（magic 检查在 CRC 之前，无需重封）
  CHECK(rejected(fx.buf));
}

MINI_TEST(load_rejects_bad_version) {
  Fixture fx;
  wrU16(&fx.buf, 4, 2);  // version = 2（未来版本）
  CHECK(rejected(fx.buf));
  wrU16(&fx.buf, 4, 0);  // version = 0
  CHECK(rejected(fx.buf));
  wrU16(&fx.buf, 4, kFormatVersion);  // 恢复 → 应能加载（补丁机制自检）
  SessionState ok;
  CHECK(load(fx.buf.data(), fx.buf.size(), &ok));
}

MINI_TEST(load_rejects_payload_len_mismatch) {
  Fixture fx;
  const uint32_t plen = rdU32(fx.buf, 6);
  wrU32(&fx.buf, 6, plen + 1);  // 声称更长
  CHECK(rejected(fx.buf));
  wrU32(&fx.buf, 6, plen - 1);  // 声称更短
  CHECK(rejected(fx.buf));
  wrU32(&fx.buf, 6, plen);  // 恢复 → 应能加载
  SessionState ok;
  CHECK(load(fx.buf.data(), fx.buf.size(), &ok));
}

MINI_TEST(load_rejects_crc_mismatch) {
  Fixture fx;
  const uint32_t plen = rdU32(fx.buf, 6);
  fx.buf[kHeaderSize + plen / 2] ^= 0xFF;  // 翻转 payload 中间 1 字节，不修 CRC
  CHECK(rejected(fx.buf));
}

MINI_TEST(load_rejects_truncation_at_every_section) {
  Fixture fx;
  for (size_t i = 0; i < fx.secs.size(); ++i) {
    // 在第 i 节长度前缀处截 1 字节。
    std::vector<uint8_t> cut(fx.buf.begin(), fx.buf.begin() + (fx.secs[i].off - 1));
    CHECK(rejected(cut));
  }
  // header 不完整（13 字节）。
  std::vector<uint8_t> cutHeader(fx.buf.begin(), fx.buf.begin() + (kHeaderSize - 1));
  CHECK(rejected(cutHeader));
  // 空缓冲。
  CHECK(rejected(std::vector<uint8_t>()));
}

MINI_TEST(load_failure_leaves_out_untouched) {
  const SessionState orig = makeFullState();
  std::vector<uint8_t> buf;
  CHECK(save(orig, &buf));
  SessionState out = orig;  // 预填完整状态
  for (size_t cut = 0; cut < buf.size(); cut += 97) {  // 多个截断点
    std::vector<uint8_t> part(buf.begin(), buf.begin() + cut);
    CHECK(!load(part.data(), part.size(), &out));
  }
  checkSame(orig, out);  // 深度比较：失败路径完全未触碰 out
}

MINI_TEST(load_rejects_bad_dims) {
  rejectI32(kSiDims, 0, 0);             // rows=0（cols 仍 80 → 非法“半空”状态）
  rejectI32(kSiDims, 0, kMaxRows + 1);  // rows 超上限
  rejectI32(kSiDims, 0, -1);            // rows 为负
  rejectI32(kSiDims, 4, 0);             // cols=0（rows 仍 24）
  rejectI32(kSiDims, 4, kMaxCols + 1);  // cols 超上限
  rejectI32(kSiDims, 4, -3);            // cols 为负
}

MINI_TEST(load_rejects_grid_count_mismatch) {
  rejectU32(kSiGrid, 0, uint32_t(kRows * kCols - 1));  // cell 数与 rows*cols 矛盾
  rejectU32(kSiGrid, 0, uint32_t(kRows * kCols + 1));  // 节数据不足（截断）
  rejectU32(kSiGrid, 0, 0);                            // rows/cols 非零但 cell 数为 0
  {  // grid 节长度前缀与实际矛盾：声称多/少 8 字节
    Fixture fx;
    wrU32(&fx.buf, fx.off(kSiGrid) - 4, uint32_t(fx.len(kSiGrid) + 8));
    fixCrc(&fx.buf);
    CHECK(rejected(fx.buf));
    wrU32(&fx.buf, fx.off(kSiGrid) - 4, uint32_t(fx.len(kSiGrid) - 8));
    fixCrc(&fx.buf);
    CHECK(rejected(fx.buf));
  }
}

MINI_TEST(load_rejects_cell_style_out_of_range) {
  // grid 首格 style == styles.size()（下标越界）；grid: count@0, cell0.style@8
  rejectU16(kSiGrid, 8, kFixtureStyles);
  // scrollback 行 0 首格 style 越界；行 0: cellCount@4, cell0.style@12
  rejectU16(kSiScrollback, 12, kFixtureStyles);
}

MINI_TEST(load_rejects_styles_over_limit) {
  rejectU32(kSiStyles, 0, uint32_t(kMaxStyles + 1));  // count > kMaxStyles
  rejectU32(kSiStyles, 0, uint32_t(kMaxStyles));     // 合法上限但节数据不足
  rejectU32(kSiStyles, 0, 3);                        // count=3 但节内 4 个 → 节体未用尽
}

MINI_TEST(load_rejects_style_enum_out_of_range) {
  // style[1].fg.kind = 3（> kColorRgb）；style 布局 12 B，fg.kind 首字节
  rejectU8(kSiStyles, 4 + kStyleRec, 3);
  // style[2].attr 的 Underline 域 = 6（> kUlDashed）；attr 位于 style 偏移 10
  rejectU16(kSiStyles, 4 + 2 * kStyleRec + 10, uint16_t(6u << kUlShift));
}

MINI_TEST(load_rejects_mark_out_of_bounds) {
  // marks: count@0, mark0.row@4, mark0.col@8, mark0.cps 数@12
  rejectI32(kSiMarks, 4, kRows);   // gridMark row == rows
  rejectI32(kSiMarks, 4, -1);      // 行为负
  rejectI32(kSiMarks, 8, kCols);   // 列 == cols
  rejectU8(kSiMarks, 12, 17);      // cps 数 17（> 16）
  // scrollback 行 0 的 mark：row == lineCount / col == cols（越界）
  rejectI32(kSiScrollback, kSbLine0Mark, 100);
  rejectI32(kSiScrollback, kSbLine0Mark + 4, kCols);
}

MINI_TEST(load_rejects_tabstops_size_mismatch) {
  rejectU32(kSiTabs, 0, uint32_t(kCols - 1));  // count != cols
  rejectU32(kSiTabs, 0, uint32_t(kCols + 1));
  rejectU8(kSiTabs, 4, 2);  // 值域：首个 stop = 2
}

MINI_TEST(load_rejects_oversized_title) {
  rejectU32(kSiTitle, 0, uint32_t(kMaxTitleBytes + 1));  // > 4 KiB → 长度规则
  rejectU32(kSiTitle, 0, uint32_t(kMaxTitleBytes));      // 合法上限但数据不足
}

MINI_TEST(load_rejects_scrollback_row_width_mismatch) {
  rejectU32(kSiScrollback, 4, uint32_t(kCols - 1));  // 行 0 cellCount != cols
  rejectU32(kSiScrollback, 4, uint32_t(kCols + 1));
}

MINI_TEST(load_rejects_scrollback_line_count_over_limit) {
  rejectU32(kSiScrollback, 0, uint32_t(kMaxScrollbackSave + 1));
}

MINI_TEST(load_rejects_sgr_stack_over_limit) {
  rejectU32(kSiSgr, 0, uint32_t(kMaxSgrStack + 1));  // 17 > 16
  rejectU32(kSiSgr, 0, uint32_t(kMaxSgrStack));      // 16 > 实际 2，节数据不足
}

MINI_TEST(load_rejects_bool_and_enum_domains) {
  rejectU8(kSiModes, 0, 2);    // cursorVisible = 2（布尔值域）
  rejectU8(kSiCursor, 8, 2);   // wrapPending = 2
  rejectU8(kSiCursor, 9, 3);   // cursorShape = 3（枚举值域）
  rejectU8(kSiMouse, 0, 5);    // mouseMode = 5
  rejectU8(kSiMouse, 1, 4);    // mouseEncoding = 4
  rejectU8(kSiCharsets, 0, 2); // g0DecGraphics = 2
}

MINI_TEST(load_rejects_trailing_payload_bytes) {
  Fixture fx;
  const uint32_t plen = rdU32(fx.buf, 6);
  for (int i = 0; i < 5; ++i) fx.buf.push_back(0);  // 13 节之后的多余字节
  wrU32(&fx.buf, 6, plen + 5);
  fixCrc(&fx.buf);
  CHECK(rejected(fx.buf));
}
