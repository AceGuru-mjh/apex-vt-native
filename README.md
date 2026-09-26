<div align="center">

# apex-vt-native

**C++17 zero-allocation VT/ANSI terminal emulation core + JNI bridge for Android**

[![native CI](https://github.com/AceGuru-mjh/apex-vt-native/actions/workflows/native.yml/badge.svg?branch=main)](https://github.com/AceGuru-mjh/apex-vt-native/actions/workflows/native.yml)
<img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white" alt="C++17"/>
<img src="https://img.shields.io/badge/ABI-arm64__v8a·armeabi--v7a·x86__64-3DDC84?logo=android&logoColor=white" alt="ABIs"/>
<img src="img.shields.io/badge/deps-0-informational" alt="zero dependencies"/>
<img src="https://img.shields.io/badge/license-MIT-lightgrey" alt="MIT"/>

</div>

---

## What is this

The native hot-path VT engine for
[Android-Guru-Agent](https://github.com/AceGuru-mjh/Android-Guru-Agent).
It replaces the pure-Kotlin `terminal-emulator` hot path
(`Utf8Decoder → VtParser → TerminalCore → ScreenBuffer`) with a C++17 core
embedded via JNI, keeping a **100% behavioral port** of the Kotlin semantics
(ATR 2.1 PR #53 + T82/T85 hardening) while eliminating the per-character
object allocation that made `cat 50MB ≈ 50M Kotlin objects` (GC pressure,
jank, OOM mitigations).

```
            Kotlin (before)                          C++ (this library)
PTY bytes ──► Utf8Decoder ──► VtParser ──► TerminalCore ──► ScreenBuffer      per char:
              (Char boxes)   (sealed Event   (data-class      (Array<Array<   2-5 heap
                             allocs/closure) TerminalCell      TerminalCell>)  allocs
                             per code point) allocs/mutations)

PTY bytes ──► feed(bytes) ─────────────► flat 8-byte Cell array               per char:
             (UTF-8 decode + VT state machine +      + interned 16-bit style   0 allocs
              screen update, one JNI call)             ids (SoA)
```

| Workload | Kotlin engine (est.) | apex-vt-native (measured, x86) |
|---|---|---|
| feed throughput (pure lines) | ~5–20 MiB/s + GC | **~156 MiB/s** |
| feed throughput (mixed: SGR/CJK/redraw) | — | **~97 MiB/s** |
| renderSnapshot (80×200 + 400 scrollback) | full object rebuild | **~120 µs/call** |
| memory per screen | 1920 objects + scrollback 80k objects | **1920×8 B POD + ring, bounded** |

> Measured on a shared CI vCPU (Xeon); arm64 on-device numbers will differ
> but the *allocation profile* — the thing that matters for Android GC — is
> zero on every platform.

## Design

- **8-byte POD cells** — `struct Cell { u32 cp; u16 styleId; u16 flags; }`,
  whole screen is one flat array, cache-friendly.
- **Style interning** — a bounded table (4096 entries, open addressing) maps
  styles to 16-bit ids; cells reference ids instead of structs.
- **Zero-allocation hot path** — parser state machine + UTF-8 decoder +
  screen update run with no heap traffic; all buffers are member state reused
  across feeds.
- **ASCII fast path** — runs of plain printable ASCII are processed with a
  cached row pointer and one dirty-region mutation per touched row.
- **Bounded everywhere** — CSI/OSC/DCS buffers, mutation list (folds to
  `FULL` at 4096, parity with Kotlin `BoundedMutationList`), scrollback ring
  (rows recycle storage), clipboard queue (8).
- **Parity by construction** — 129 host tests port the Kotlin golden suite
  (wrap semantics, DECOM, colon SGR, alt-screen 1049, OSC 52, DA/DSR
  responses, scrollback baseline monotonicity, …).
- **JNI** — one call per PTY read (`GetPrimitiveArrayCritical`), one flat
  `IntArray` per snapshot; strings travel as UTF-16 via `NewString`
  (supplementary-safe).

## Layout

```
include/apex/vt/     public headers (vt_engine.h, vt_types.h, vt_version.h)
src/                 engine implementation (style/width/screen/parser/engine)
src/jni/             Android JNI bridge → libvt_native.so
kotlin/              reference Kotlin wrapper (vendored into the main repo)
tests/               mini_test + parser/screen/engine/perf suites
tests/fuzz_feed.cpp  deterministic fuzz harness (invariant + crash guard)
tests/jni_stub/      stub jni.h for host compile-checks of the bridge
.github/workflows/   native.yml — host tests + NDK cross-compile + release
```

## Build

### Host (tests / benchmark / fuzz)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DAPEX_VT_BUILD_TESTS=ON -DAPEX_VT_BUILD_FUZZ=ON
cmake --build build -j
./build/vt_tests        # 129 tests incl. throughput floor
./build/vt_fuzz         # deterministic fuzz (seed, iterations)
```

### Android (NDK)

```bash
cmake -S . -B build-arm64 \
      -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
      -DCMAKE_BUILD_TYPE=Release -DAPEX_VT_BUILD_JNI=ON
cmake --build build-arm64 -j    # → build-arm64/libvt_native.so
```

ABIs: `arm64-v8a`, `armeabi-v7a`, `x86_64`. minSdk 26 (Android 8.0) to match
the host app. 16 KB page alignment is set for Android 15+ compatibility.

### CI

`native.yml` (GitHub Actions):
1. **host-tests** — builds Release, runs the 129-case parity suite, the fuzz
   harness, and a JNI-bridge type-check against stub headers.
2. **android-ndk** — cross-compiles `libvt_native.so` for all three ABIs and
   uploads artifacts.
3. **release** — on `v*` tags, bundles the `.so` files into a draft GitHub
   Release.

## Integration into Android-Guru-Agent

The main repo vendors `src/` + `include/` + the Kotlin wrapper into the
`:terminal-native` Gradle module (AGP + CMake, same pattern as the existing
`:platform:terminal` PTY code). `RealVirtualTerminal` picks the native engine
through `VtEngineFactory` when `libvt_native.so` loads; otherwise it falls
back to the pure-Kotlin `TerminalCore` — unit tests and JVM CI are unaffected.
See the main repo's `terminal-native/VENDOR.md` for the vendoring contract.

## Kotlin semantics ported (checklist)

- wrapPending set only when a char actually lands in the last column (T85)
- C1 (8-bit) controls → 7-bit equivalents; 0xA0..0xBF → U+FFFD
- SGR colon sub-params: `38:2:r:g:b`, `38:2:cs:r:g:b`, `38:5:n`, `4:x`
- explicit CSI param `0` = default; overflow params → 0 (`toIntOrNull` parity)
- erase carries current style (bg persistence); scroll blank = default style
- ED 3 clears main-screen scrollback only; `clear` command parity
- DECSTBM/DECOM clamping; DECSTR vs RIS; DECSET/DECRST 47/1047/1049
- DA1 = `ESC[?6c`, DA2 = `ESC[>0;276;0c`, DSR 5/6 responses, CPR 1-based
- DEC Special Graphics via `ESC(0`; OSC 0/1/2 title; OSC 52 clipboard
  (standard base64, ≤8 pending, drop-oldest)
- mutations: 8 types, bounded 4096, fold to `FULL`; every C0/CSI emits a
  cursor-row mutation (even when the op is a no-op)
- scrollback `linesEver` monotonic across eviction and `clear()`
- wide-char trail repair on overwrite; combining marks attach to the base
  cell and follow ICH/DCH shifts

## License

MIT — see [LICENSE](LICENSE).

## v0.2 — Foundation Capabilities

v0.2 adds ~10k lines of **necessary basic capabilities** on top of the v0.1
zero-allocation VT engine core (no speculative features — everything here is
what a production Android terminal needs):

| Module | Capability |
|---|---|
| `vt_unicode_data` / `vt_grapheme` | Full East Asian Width tables + UAX #29 extended grapheme clusters (GB1–GB13, emoji ZWJ families, RI flags) — correct cursor movement, selection and copy |
| `vt_input` | Key encoding honoring DECCKM / DECKPAM / modifyOtherKeys 1-2 / Kitty; paste sanitization + bracketed-paste wrapping (anti sequence-injection) |
| `vt_mouse` | Mouse reporting: X10 / normal / button / any-event tracking × X11 / UTF-8 / SGR / urxvt encodings, byte-exact |
| `vt_lines` / `vt_search` / `vt_reflow` | Logical line assembly over wrapped rows; Ctrl+F search (cross-wrap, case-folded, whole-word) over screen + scrollback; **resize reflow — content survives rotation / split-screen / foldables** |
| `vt_session` | Binary session persistence (CRC-guarded, strict validation) — terminal state survives Android process death |
| engine | OSC 8 hyperlinks (sparse spans, bounded table), DECRQM, DECSED/DECSEL/DECSCA protection, DECERA/DECSERA/DECCRA/DECFRA rectangles, XTPUSHSGR stack, focus reporting (1004), alternate scroll (1007), sync output (2026), OSC 4/10/11/12 color queries, touch selection (word/line, global rows) |
| JNI / Kotlin | All of the above exposed through one-call flat-array snapshots + mode-aware input encoders |

370 host tests (v0.1 parity + 241 new), fuzz clean, ASAN/UBSAN clean.
