# To do

Work that was planned and left for later, with what's known so far, so it can be picked up
where it was left.

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
  on Windows' (PE). So it needs the Linux builds first, official ones as macOS releases are
  (`chromium/args.release.gn`).
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
- Releases are official builds with Google's PGO profiles (`chromium/args.release.gn`). Linux
  has profiles for x64 only; Windows for x64 and arm64. `sync.sh` fetches those of the
  `target_os` in `.gclient`, only `mac` for now.
- Official builds turn on Clang's CFI on Linux x64 (`is_cfi`), which needs checking against
  Oblada's code.
- On Linux, Chromium's code leaves GPU rasterization off (`kDefaultEnableGpuRasterization`
  in `gpu/config/gpu_finch_features.cc` is on only for macOS, Windows, ChromeOS and Android),
  and Oblada has no field trials that might turn it on, as Chrome's may. Check
  `oblada://gpu` on the Linux build before deciding whether to turn it on, with GPU drivers'
  blocklist still applying.

## Safe Browsing v5, before 2027-03-31

Google ends support for the Safe Browsing APIs v4 on 2027-03-31 (they may keep
working after). Chromium 156 still downloads its local lists with v4
(`kLocalListsUseSBv5`, off by default); Brave tracks the same switch
(brave/brave-browser#56023). The key releases have (`make safe-browsing-key`) is for
the same service, `safebrowsing.googleapis.com`, and works for v5 too: when a newer
Chromium turns v5 on, or before the date, turn `kLocalListsUseSBv5` on and check
that v5's requests (`v5_update_protocol_manager.cc`, `v5_search_hashes_util.cc`) get
Oblada's key rather than `google_apis::GetAPIKey()`: today only
`GetSBProtocolConfig()` gives it (`sb_protocol_config.cc`'s patch), and v5's code reads
`google_apis` directly.
