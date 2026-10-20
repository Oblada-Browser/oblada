# Oblada

Oblada is a Chromium-based browser for macOS: Zen's design (tabs, address bar and workspaces in a left sidebar, compact mode, split view), Firefox's containers (a storage partition per container, "always open this site in…"), and extensions from both the Chrome Web Store and Firefox Add-ons (`.xpi` converted on install, Firefox's `contextualIdentities` API). The user writes in Italian: reply in Italian.

## Layout

It's a Chromium fork laid out like Brave. Chromium itself is never committed; the checkout lives in `~/chromium` (`$OBLADA_CHROMIUM`), and its sources are in `~/chromium/src`.

| Path | What it is |
| --- | --- |
| `chromium/CHROMIUM_VERSION` | the pinned Chromium release |
| `chromium/args.gn` | build args, copied into the build directory by `build.sh` |
| `chromium/oblada/` | Oblada's own code (namespace `oblada`), linked into the checkout as `src/oblada`; sources are listed in `oblada/browser/sources.gni` |
| `chromium/patches/` | Oblada's edits to Chromium's files, one patch per file |
| `chromium/overrides/` | files that replace Chromium's whole (the logo's images and vector icons), copied over them at the same paths |
| `chromium/scripts/` | `sync.sh`, `build.sh`, `patches.sh`, `rebrand.py`, `smoke.mjs`, `screenshot.mjs`, `lib/oblada.mjs`, `dmg.sh` and `release.sh` (see Releases) |
| `branding/` | the logo, an occhiata's tail: `logo.py` draws it, `export.sh` writes every image and vector icon Chromium shows its logo with into `chromium/overrides/`. `vector_icon.py --svg <size>` gives the one-color glyph as an SVG path for the `chrome-product` icon of WebUI iconsets, which are carried as patches. `dmg/` is the DMG window's background: `render.mjs` draws `background.html` into its PNGs |

## Workflow

- Change Chromium files in `~/chromium/src`, then run `chromium/scripts/patches.sh update` to rewrite `chromium/patches/`. Mark Oblada's lines in Chromium files with `// Oblada` comments. Put new code in `chromium/oblada/`, not in Chromium's tree, and list it in `sources.gni`. Keep `PATCHES.md` in step: a new or removed patch, or one that changes purpose or depends on another, gets its entry updated.
- `chromium/scripts/build.sh` applies what isn't applied yet (patches, overrides, and the "Chromium" → "Oblada" renaming of strings in every language by `rebrand.py`), then builds. Don't hand-edit `*chromium_strings*` files.
- Build output is `Oblada.app` in `out/Oblada`, or in the checkout's existing build directory (the scripts find it). Renaming a build directory means a full rebuild of about 6 hours. A typical incremental build takes a few minutes; run long ones in the background.
- User-visible strings for Oblada's own UI live in `oblada/browser/l10n.cc`, in English and Italian. Prefs are named `oblada.*`. Internal pages are shown as `oblada://` (they are `chrome://` underneath).
- Commit only when the user asks.

## Releases

Releases are published on GitHub (`Oblada-Browser/oblada`) with `chromium/scripts/release.sh <tag> <notes.md>`; the DMG is never committed. Tags are Chromium's version and Oblada's revision on it (`155.0.8059.85-1`, `-2`…). The release is marked as the latest: the website's download button leads to `/releases/latest`. `dmg.sh` signs the app ad hoc as a whole (the build is only linker-signed, which macOS calls damaged once downloaded) and lays out the DMG's window with dmgbuild, without opening Finder. Release notes are in English: what Oblada is, crediting Zen, how to install it past Gatekeeper, known limits.

## Testing: never take the user's focus

The user keeps working while tests run. Everything runs headless, driven over the DevTools protocol, with a throwaway profile:

- `node chromium/scripts/smoke.mjs`: about 40 checks, takes a few minutes. Its final check fails if Oblada ever came to the front. `a tab dragged onto the page goes next to it (split view)` is known to be flaky in full runs.
- `node chromium/scripts/screenshot.mjs out.png [--compact] [--do="…"]`: the window including the browser UI, plus `out.layout.txt` (every view and its bounds) and `out.popup.png` for an open bubble. The `--do` actions are documented in `oblada/browser/debug/window_snapshots.cc`: command ids, `click:<view class>`, `split`, `drag:`/`drop`/`grip:`, `type:`, `press:<x>,<y>`, `select-to:<n>`, `open:<url>`, `force-click`, `expand`, `escape`, `wait:<ms>`, `ws-new`/`ws-next`/`ws-prev`/`ws-icon:`/`ws-color:`/`ws-tint:`/`ws-picker`/`ws-container:`, `swipe:`/`swipe-timed:`/`swipe-end`, and `film:<count>:<ms>` (frames of an animation).
- `lib/oblada.mjs` launches an APFS clone, `Oblada Test.app`, with its own bundle id, so that clicking Oblada in the Dock never reaches a test instance. It also passes `--use-mock-keychain`.
- Never launch the user's real profile (`~/Library/Application Support/Oblada`), never `pkill` Chromium or Oblada broadly (kill only your own PIDs), and ask before anything that needs a visible window.
- Crash dumps are in `~/Library/Application Support/Oblada/Crashpad/pending/` (test runs write there too). The build has no symbols: `strings -n 8 <dmp> | grep -E 'FATAL|Check failed'` gives the failing line. For a crash without one (e.g. a segfault), macOS's report `~/Library/Logs/DiagnosticReports/Oblada-*.ips` (JSON) names the functions on the crashing thread.

## Code style

Follow Chromium's style. Comments are short sentences saying what something is for, written for someone reading the code cold. Don't add comments that only narrate the change.
