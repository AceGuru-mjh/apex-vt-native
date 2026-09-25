// mini_test — a ~100-line zero-dependency test framework (self-contained by
// design: the library must build with no external packages on host CI).
#pragma once

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
inline int& checks() {
  static int c = 0;
  return c;
}
inline const char*& current() {
  static const char* cur = "";
  return cur;
}

inline bool registerCase(const char* name, std::function<void()> fn) {
  registry().push_back(TestCase{name, fn});
  return true;
}

inline std::string toStr(const char* s) { return s ? s : "(null)"; }
inline std::string toStr(const std::string& s) { return "\"" + s + "\""; }
inline std::string toStr(bool b) { return b ? "true" : "false"; }

// One generic sink for all arithmetic types (int, unsigned, long, uint32_t...).
template <typename T>
inline std::enable_if_t<std::is_arithmetic<T>::value, std::string> toStr(T v) {
  return std::to_string(v);
}

inline int runAll() {
  int n = 0;
  for (const TestCase& c : registry()) {
    ++n;
    current() = c.name;
    int before = failures();
    std::printf("[ RUN  ] %s\n", c.name);
    try {
      c.fn();
    } catch (const std::exception& ex) {
      ++failures();
      std::printf("  UNCAUGHT EXCEPTION: %s\n", ex.what());
    }
    bool ok = failures() == before;
    std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
  }
  std::printf("\n%d tests, %d failed checks\n", n, failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace minitest

#define MINI_TEST(name)                                                \
  static void mini_test_fn_##name();                                   \
  static bool mini_test_reg_##name =                                   \
      ::minitest::registerCase(#name, mini_test_fn_##name);            \
  static void mini_test_fn_##name()

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++::minitest::checks();                                             \
    if (!(cond)) {                                                      \
      ++::minitest::failures();                                         \
      std::printf("  FAIL %s:%d  CHECK(%s) in %s\n", __FILE__, __LINE__, \
                  #cond, ::minitest::current());                        \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                                  \
  do {                                                                                  \
    ++::minitest::checks();                                                             \
    if (!((a) == (b))) {                                                               \
      ++::minitest::failures();                                                         \
      std::printf("  FAIL %s:%d  CHECK_EQ(%s, %s)  actual=%s expected=%s  (%s)\n",      \
                  __FILE__, __LINE__, #a, #b, ::minitest::toStr(a).c_str(),             \
                  ::minitest::toStr(b).c_str(), ::minitest::current());                 \
    }                                                                                   \
  } while (0)
