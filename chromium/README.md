# Oblada on Chromium

Oblada as a Chromium fork, laid out like Brave: Chromium itself is never committed here.

| Path | What it is |
| --- | --- |
| `CHROMIUM_VERSION` | the Chromium release Oblada is built on |
| `args.gn` | build configuration (copied to `out/Oblada/args.gn`) |
| `oblada/` | Oblada's own C++/WebUI code, linked into the checkout as `src/oblada` |
| `patches/` | Oblada's edits to Chromium files, one patch per file (see [PATCHES.md](../PATCHES.md)) |
| `overrides/` | Oblada's logo files, which replace Chromium's at the same paths in the checkout (made by `branding/export.sh`) |
| `scripts/` | `sync.sh`, `build.sh`, `patches.sh`, `smoke.mjs`, `screenshot.mjs`, `dmg.sh`, `release.sh` |

The checkout lives in `~/chromium` (override with `OBLADA_CHROMIUM`). It takes about 35 GB of
sources and toolchains, plus about 60 GB per build directory.

```sh
chromium/scripts/sync.sh      # check out CHROMIUM_VERSION, apply patches (re-run after bumping it)
chromium/scripts/build.sh     # build Oblada.app (in out/Oblada, or the checkout's existing build directory)
chromium/scripts/patches.sh update   # after editing Chromium files in ~/chromium/src
node chromium/scripts/smoke.mjs      # check the patches in the build (headless, no window)
node chromium/scripts/screenshot.mjs out.png   # the window, browser UI included, headless
```

`smoke.mjs` also installs uBlock Origin and Multi-Account Containers from
addons.mozilla.org; `OBLADA_SMOKE_NETWORK=0` skips that. `screenshot.mjs` writes
`out.layout.txt` next to the image: every view of the window and where it is, plus the
window controls (which macOS draws, and the screenshot marks). `--compact` shows the
compact sidebar; `--do="39001 click:SitePanelButton"` runs browser commands and clicks
views first (a bubble it opens is saved as `out.popup.png`).

Oblada's own extension APIs (Firefox's `contextualIdentities`, for Firefox add-ons) are
built from `oblada/common/extensions/api` and registered next to Chrome's.

Releases are on GitHub, with a DMG to download (DMGs are never committed):

```sh
chromium/scripts/dmg.sh       # Oblada-<version>-arm64.dmg, in the build directory
chromium/scripts/release.sh 155.0.8059.85-2 /tmp/notes.md   # build, pack and publish the commit
```

`release.sh` wants the commit pushed and nothing uncommitted, in the repo or in the
checkout's Chromium files. It marks the release as the latest, where the website's
download button leads. Tags are Chromium's version and Oblada's revision on it. The app
in the DMG is signed ad hoc, as a whole: there's no Developer ID yet, so macOS asks to
"Open Anyway" the first time. `node branding/dmg/render.mjs` redraws the DMG's background
from `branding/dmg/background.html` (`--mock <dir>` also shows it with Finder's icons).

Updating Chromium: change `CHROMIUM_VERSION`, run `sync.sh`, fix any patch that no longer
applies, then `patches.sh update`. [PATCHES.md](../PATCHES.md) says what each patch is for
and which ones depend on each other; keep it up to date when patches change.
