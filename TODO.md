# To do

Work that was planned and left for later, with what's known so far, so it can be picked up
where it was left.

## Official builds for releases

Oblada's releases are built as plain release builds (`chromium/args.gn`), not as "official"
ones. Chromium asks for `is_official_build = true` in any build that ships to users
(`build/config/BUILDCONFIG.gn`); without it the build is an "unsafe developer build"
(`base/BUILD.gn`). Brave and Helium release official builds with Google's PGO profiles.

What `is_official_build = true` turns on, for Chromium 156, on macOS, Linux and Windows:
- **PGO** (`chrome_pgo_phase = 2`, `build/config/compiler/pgo/pgo.gni`) with Google's
  profiles for the pinned version (`chrome/build/{mac-arm,linux,win64,win-arm64}.pgo.txt`; the
  mac-arm one is about 89 MB). Google measured up to 10% faster page loads
  ([Chromium blog, 2020](https://blog.chromium.org/2020/08/chrome-just-got-faster-with-profile.html)).
- **ThinLTO** with optimizations (`use_thin_lto`, `thin_lto_enable_optimizations`). With PGO
  and function ordering, +25.8% on Speedometer on Windows
  ([Chromium blog, 2021](https://blog.chromium.org/2021/12/faster-chrome-let-the-compiler-do-the-work.html)).
- **V8's builtins optimized with profiles** (`v8_enable_builtins_optimization`, tied to
  `chrome_pgo_phase == 2` in `v8/gni/v8.gni`).
- Stripped binaries and dSYMs on macOS (`build/config/apple/symbols.gni`), CFI on Linux x64.

Oblada's patches don't stop the profiles from applying: functions they change only lose
their profile, and `build/config/compiler/pgo/BUILD.gn` already silences the warnings about
it (`-Wno-profile-instr-out-of-date`, `-Wno-profile-instr-unprofiled`,
`-Wno-backend-plugin`).

### Plan

1. Fetch the PGO profiles: `"checkout_pgo_profiles": True` in `~/chromium/.gclient`'s
   `custom_vars` (written by `chromium/scripts/sync.sh`), then `gclient runhooks`. Only the
   profiles of the `target_os` in `.gclient` are downloaded.
2. Keep the current build directory for development (fast incremental builds, CHECK messages)
   and add a second one for releases, e.g. `out/Release`, with `chromium/args.gn` plus
   `is_official_build = true`. Every incremental build there relinks with ThinLTO.
3. Cap the ThinLTO cache, which by default may grow to 10% of the disk or 40 GB
   (`build/config/compiler/BUILD.gn`), or turn it off (`thin_lto_enable_cache = false`).
4. Make `build.sh` able to build the release directory, and `dmg.sh`/`release.sh` pack from
   it.
5. Compare the two builds on Speedometer 3 before switching releases over.

### Costs

- The first build is a full one: about 6–8 hours on the M4 (the development one takes ~5).
  The final link with ThinLTO needs a lot of memory; the Mac has 24 GB, so limit its jobs if it
  swaps.
- About 15 GB more disk for the second build directory, plus the ThinLTO cache. Check
  `df -g ~` first.
- Official builds drop CHECK failure messages and their file and line (`base/check.h`,
  `CHECK_WILL_STREAM()`), so `strings <dump> | grep 'Check failed'` (CLAUDE.md) no longer works
  on released builds.
  - Part of `chromium/patches/base-check.cc.patch` becomes redundant: official builds already
    don't crash on `DUMP_WILL_BE_CHECK`.

### Open question: symbols for crash dumps

To decide when official builds start. Until then, releases have no symbols, as now.
- Keep them locally: `symbol_level = 1` and the dSYMs kept on the Mac, not shipped. Crash dumps
  from released builds can be read, for a few GB per release and a longer build.
- No symbols: smaller and faster builds; crashes are understood by reproducing them on the
  development build.

### Not worth it

What Thorium adds on top of an official build: -O3 everywhere, AVX2/AVX-512 builds (they crash
on older CPUs), `-ffp-contract=fast` (changes floating-point results), lower libc++ hardening,
LLVM's GVN hoist and Polly. Its gains over Chrome aren't shown by independent measurements, and
the developer behind most of these flags calls -O3's gain trivial
([RobRich999/Chromium_Clang#26](https://github.com/RobRich999/Chromium_Clang/issues/26#issuecomment-951919715)).
`-mcpu=apple-m1` is already clang's default for arm64 macOS.

## BOLT on Linux (highly experimental)

An experiment, not a plan: worth trying only once official Linux builds exist, and only if it
measures well.

[BOLT](https://github.com/llvm/llvm-project/tree/main/bolt) (LLVM's post-link optimizer)
rewrites a linked binary using a profile of it running: it reorders functions and their
blocks so the code that runs together sits together. Amir Ayupov tried it on Chromium
([BOLTing Chromium](https://aaupov.github.io/blog/2022/11/12/bolt-chromium), 2022), on Linux
x86-64 (an Intel i7-12700K), over an official build with Google's PGO profiles: Speedometer 2
went from 382 to 404 runs a minute (+5.8%), with 21% fewer instruction cache misses and 32%
fewer iTLB misses.

- **Linux only.** BOLT works on ELF binaries, x86-64 and AArch64; not on macOS's (Mach-O) nor
  on Windows' (PE). So it needs the Linux builds and the official builds above first.
- **Steps, as in the article.**
  1. Link the `chrome` binary with `-Wl,--emit-relocs`.
  2. Record a profile running Speedometer: `perf record -e cycles:u -j any,u`. Branch records
     need hardware support (Intel's LBR, and AMD's on recent CPUs), often missing in virtual
     machines and CI. Without them there's BOLT's instrumentation mode, which is slower, or
     plain samples, whose profile is worse.
  3. Convert the profile with `perf2bolt`.
  4. Rewrite the binary with `llvm-bolt` (block and function reordering, splitting cold
     code), skipping V8's builtins (`-skip-funcs=Builtins_.*`).
- **Costs and risks.**
  - **CFI:** BOLT didn't handle the symbols Clang's CFI adds, which official Linux x64 builds
    turn on (`is_cfi`), so the author built without it. That gives up a security feature;
    check whether newer BOLT handles CFI.
  - **The tool:** Chromium's toolchain doesn't ship `llvm-bolt`. Chromium only uses BOLT on its
    own clang (`tools/clang/scripts/build.py --bolt`), so it would come from a distribution's
    LLVM or a build of it.
  - **The profile:** it belongs to one Chromium version and one build. It would have to be made
    for each release and shipped with the sources for Flathub's builders to apply it, and
    distributions packaging Oblada would likely skip it.
  - **What it can't reach:** V8's JIT code, about a fifth of the time in the article, is out of
    its reach.
  - **The measurements:** the article trained and measured on the same benchmark, on one
    machine, so its +5.8% is likely the best case. Measure Speedometer 3 and everyday pages
    instead.
- Thorium has a `use_bolt` build arg for it, off by default.

## Linux and Windows releases

Linux first, as a Flatpak on Flathub and as a plain binary release for distributions to
package; then Windows.

- Chromium's prebuilt clang has no Linux arm64 host (`tools/clang/scripts/update.py` offers
  `linux`, `mac`, `mac-arm64`, `win`), and a virtual machine on an Apple silicon Mac runs arm64
  Linux. The supported way is an x86_64 Linux machine (CI or a remote one); on arm64 Linux, the
  distribution's LLVM and Rust would be needed, as distributions do.
- Linux has PGO profiles for x64 only; Windows for x64 and arm64.
- On Linux, Chromium's code leaves GPU rasterization off (`kDefaultEnableGpuRasterization`
  in `gpu/config/gpu_finch_features.cc` is on only for macOS, Windows, ChromeOS and Android),
  and Oblada has no field trials that might turn it on, as Chrome's may. Check
  `oblada://gpu` on the Linux build before deciding whether to turn it on, with GPU drivers'
  blocklist still applying.
