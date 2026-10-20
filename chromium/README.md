# Oblada on Chromium

Oblada as a Chromium fork, laid out like Brave: Chromium itself is never committed here.

| Path | What it is |
| --- | --- |
| `CHROMIUM_VERSION` | the Chromium release Oblada is built on |
| `args.gn` | build configuration (copied to `out/Oblada/args.gn`) |
| `oblada/` | Oblada's own C++/WebUI code, linked into the checkout as `src/oblada` |
| `patches/` | Oblada's edits to Chromium files, one patch per file (see [PATCHES.md](../PATCHES.md)) |
| `overrides/` | Oblada's logo files, which replace Chromium's at the same paths in the checkout (made by `branding/export.sh`) |
| `scripts/` | `sync.sh`, `build.sh`, `patches.sh` |

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

Updating Chromium: change `CHROMIUM_VERSION`, run `sync.sh`, fix any patch that no longer
applies, then `patches.sh update`. [PATCHES.md](../PATCHES.md) says what each patch is for
and which ones depend on each other; keep it up to date when patches change.
