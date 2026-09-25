// Parser-level tests — ported from Kotlin TerminalCoreTest parser sections.
#include "mini_test.h"

#include "../src/vt_parser.h"

#include <string>
#include <vector>

using namespace apex::vt;

namespace {

// Test host — records emitted events.
struct TestHost {
  struct CsiEvent {
    CsiSequence seq;
  };
  std::vector<std::string> events;  // human-readable event log
  std::vector<CsiSequence> csis;
  std::vector<std::pair<int32_t, std::u16string>> oscs;
  std::vector<std::pair<char, std::string>> escs;
  int printables = 0;
  int c0s = 0;
  int unknowns = 0;
  uint32_t lastCp = 0;

  void onPrintable(uint32_t cp) {
    ++printables;
    lastCp = cp;
  }
  void onC0(uint32_t) { ++c0s; }
  void onCsi(const CsiSequence& seq) { csis.push_back(seq); }
  void onOsc(int32_t code, const std::vector<char16_t>& data) {
    oscs.emplace_back(code, std::u16string(data.begin(), data.end()));
  }
  void onEsc(char final, const char* inter, size_t n) {
    escs.emplace_back(final, std::string(inter ? inter : "", n));
  }
  void onDcs(const std::vector<char16_t>&) {}
  void onUnknown() { ++unknowns; }
};

Parser<TestHost> parser;
TestHost host;

void reset() {
  parser.reset();
  host = TestHost{};
}

void feedStr(const char* s) {
  for (const char* p = s; *p; ++p) parser.feed(uint32_t(uint8_t(*p)), host);
}

}  // namespace

MINI_TEST(decodes_simple_csi) {
  reset();
  feedStr("\x1B[2J");
  CHECK_EQ(host.csis.size(), size_t(1));
  CHECK_EQ(host.csis[0].finalByte, 'J');
  CHECK_EQ(host.csis[0].param(0, -1), 2);
  CHECK_EQ(host.csis[0].params.size(), size_t(1));
}

MINI_TEST(parses_csi_across_split) {
  reset();
  feedStr("\x1B[3");
  CHECK_EQ(host.csis.size(), size_t(0));
  feedStr("1m");
  CHECK_EQ(host.csis.size(), size_t(1));
  CHECK_EQ(host.csis[0].finalByte, 'm');
  CHECK_EQ(host.csis[0].param(0, -1), 31);
}

MINI_TEST(parses_multiple_params_and_defaults) {
  reset();
  feedStr("\x1B[1;5H");
  CHECK_EQ(host.csis.size(), size_t(1));
  CHECK_EQ(host.csis[0].param(0, -1), 1);
  CHECK_EQ(host.csis[0].param(1, -1), 5);
  // empty tokens → 0 (xterm default semantics)
  reset();
  feedStr("\x1B[;m");
  CHECK_EQ(host.csis[0].params.size(), size_t(2));
  CHECK_EQ(host.csis[0].param(0, -1), 0);
  CHECK_EQ(host.csis[0].param(1, -1), 0);
}

MINI_TEST(param_zero_is_not_default) {
  reset();
  feedStr("\x1B[0A");
  CHECK_EQ(host.csis[0].param(0, -1), 0);
}

MINI_TEST(overflow_param_becomes_zero) {
  reset();
  feedStr("\x1B[99999999999999A");  // > Int32.MAX → toIntOrNull null → 0
  CHECK_EQ(host.csis[0].param(0, -1), 0);
}

MINI_TEST(parses_private_marker) {
  reset();
  feedStr("\x1B[?25h");
  CHECK_EQ(host.csis[0].privateMarker, '?');
  CHECK_EQ(host.csis[0].finalByte, 'h');
  reset();
  feedStr("\x1B[>c");
  CHECK_EQ(host.csis[0].privateMarker, '>');
}

MINI_TEST(parses_osc_title) {
  reset();
  feedStr("\x1B]0;my title\x07");
  CHECK_EQ(host.oscs.size(), size_t(1));
  CHECK_EQ(host.oscs[0].first, 0);
  std::u16string expected = u"my title";
  CHECK(host.oscs[0].second == expected);
}

MINI_TEST(parses_osc_terminated_by_st) {
  reset();
  feedStr("\x1B]2;hello\x1B\\");
  CHECK_EQ(host.oscs.size(), size_t(1));
  CHECK_EQ(host.oscs[0].first, 2);
  CHECK(host.oscs[0].second == u"hello");
}

MINI_TEST(osc_esc_then_new_control_restarts) {
  reset();
  feedStr("\x1B]0;t\x1B[2J");  // OSC aborted by ESC [ (not ST)
  // OSC is emitted (T85 parity: emit then re-handle ESC), CSI also parsed
  CHECK_EQ(host.oscs.size(), size_t(1));
  CHECK_EQ(host.csis.size(), size_t(1));
}

MINI_TEST(unknown_csi_does_not_crash) {
  reset();
  feedStr("\x1B[9999999999999999999999999z");
  CHECK_EQ(host.csis.size(), size_t(1));  // still emitted (final 'z' unknown → ignored by host)
}

MINI_TEST(bounded_osc_discards_overlong) {
  reset();
  feedStr("\x1B]52;c;");
  for (int i = 0; i < 100100; ++i) parser.feed('A', host);  // overflow the string buffer
  feedStr("\x07");
  // The overlong string was discarded → no OSC event
  CHECK_EQ(host.oscs.size(), size_t(0));
}

MINI_TEST(c1_csi_9b_parsed_as_csi) {
  reset();
  const uint8_t seq[] = {0x9B, '2', 'J'};
  for (uint8_t b : seq) parser.feed(b, host);
  CHECK_EQ(host.csis.size(), size_t(1));
  CHECK_EQ(host.csis[0].finalByte, 'J');
  CHECK_EQ(host.csis[0].param(0, -1), 2);
}

MINI_TEST(c1_nel_85_emits_esc_e) {
  reset();
  parser.feed(0x85, host);
  CHECK_EQ(host.escs.size(), size_t(1));
  CHECK_EQ(host.escs[0].first, 'E');
}

MINI_TEST(c1_sos_pm_apc_ignored) {
  reset();
  parser.feed(0x98, host);
  parser.feed('x', host);
  parser.feed('y', host);
  parser.feed('\\', host);  // ST
  parser.feed('a', host);  // ground again
  CHECK_EQ(host.printables, 1);  // 'a' printed; 'x','y' swallowed by string-ignore
  CHECK_EQ(host.csis.size(), size_t(0));
}

MINI_TEST(truecolor_sgr_semicolon_parse) {
  reset();
  feedStr("\x1B[38;2;255;0;0m");
  CHECK_EQ(host.csis.size(), size_t(1));
  const auto& seq = host.csis[0];
  CHECK_EQ(seq.params.size(), size_t(5));
  CHECK_EQ(seq.param(0, -1), 38);
  CHECK_EQ(seq.param(1, -1), 2);
  CHECK_EQ(seq.param(2, -1), 255);
  CHECK_EQ(seq.hasSubParams(0), false);
}

MINI_TEST(colon_subparams_parse) {
  reset();
  feedStr("\x1B[38:2:255:0:0m");
  const auto& seq = host.csis[0];
  CHECK_EQ(seq.params.size(), size_t(1));  // '38' is the only token
  CHECK_EQ(seq.param(0, -1), 38);
  CHECK_EQ(seq.hasSubParams(0), true);
  uint32_t n = 0;
  const int32_t* subs = seq.subParams(0, n);
  CHECK_EQ(n, uint32_t(5));
  CHECK_EQ(subs[0], 38);
  CHECK_EQ(subs[1], 2);
  CHECK_EQ(subs[2], 255);
  CHECK_EQ(subs[3], 0);
  CHECK_EQ(subs[4], 0);
}

MINI_TEST(colon_token_with_trailing_colon) {
  reset();
  feedStr("\x1B[4:m");
  const auto& seq = host.csis[0];
  uint32_t n = 0;
  const int32_t* subs = seq.subParams(0, n);
  CHECK_EQ(n, uint32_t(2));  // Kotlin split: ["4", ""]
  CHECK_EQ(subs[0], 4);
  CHECK_EQ(subs[1], 0);
}

MINI_TEST(mixed_semicolon_and_colon_tokens) {
  reset();
  feedStr("\x1B[0;38:5;196;48:2:1:2:3m");
  const auto& seq = host.csis[0];
  // tokens split by ';': "0", "38:5", "196", "48:2:1:2:3" → 4 tokens.
  CHECK_EQ(seq.params.size(), size_t(4));
  CHECK_EQ(seq.param(0, -1), 0);
  CHECK_EQ(seq.param(1, -1), 38);
  CHECK(seq.hasSubParams(1));
  CHECK(!seq.hasSubParams(2));
  CHECK_EQ(seq.param(3, -1), 48);
  uint32_t n3 = 0;
  const int32_t* subs3 = seq.subParams(3, n3);
  CHECK_EQ(n3, uint32_t(5));  // "48:2:1:2:3" → 5 sub values
  CHECK_EQ(subs3[0], 48);
}

MINI_TEST(esc_intermediate_dispatch) {
  reset();
  feedStr("\x1B(0");  // designate DEC graphics — ESC intermediate 0x28 final '0'
  CHECK_EQ(host.escs.size(), size_t(1));
  CHECK_EQ(host.escs[0].first, '0');
  CHECK_EQ(host.escs[0].second, std::string(1, char(0x28)));
}

MINI_TEST(csi_intermediate_kept) {
  reset();
  feedStr("\x1B[1 q");  // DECSCUSR — intermediate SP then final 'q'
  CHECK_EQ(host.csis.size(), size_t(1));
  CHECK_EQ(host.csis[0].finalByte, 'q');
  CHECK_EQ(host.csis[0].intermediates, std::string(" "));
  CHECK_EQ(host.csis[0].param(0, -1), 1);
}

MINI_TEST(dcs_swallowed_until_st) {
  reset();
  feedStr("\x1BP1;2q data \x1B\\");
  CHECK_EQ(host.csis.size(), size_t(0));
  CHECK_EQ(host.printables, 0);
  // After ST, ground again
  feedStr("x");
  CHECK_EQ(host.printables, 1);
}

MINI_TEST(esc_esc_restarts_escape) {
  reset();
  feedStr("\x1B\x1B[2J");
  CHECK_EQ(host.unknowns, 1);  // first ESC ESC → Unknown
  CHECK_EQ(host.csis.size(), size_t(1));
}

MINI_TEST(del_is_c0) {
  reset();
  parser.feed(0x7F, host);
  CHECK_EQ(host.c0s, 1);
  CHECK_EQ(host.printables, 0);
}
