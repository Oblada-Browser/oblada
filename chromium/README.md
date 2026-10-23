# Oblada on Chromium

Oblada as a Chromium fork, laid out like Brave: Chromium itself is never committed here.

| Path | What it is |
| --- | --- |
| `CHROMIUM_VERSION` | the Chromium release Oblada is built on |
| `args.gn` | build configuration (copied to `out/Oblada/args.gn`) |
| `args.release.gn` | what releases add to `args.gn`: an official build, in `out/Release` |
| `oblada/` | Oblada's own C++/WebUI code, linked into the checkout as `src/oblada`; `oblada/VERSION` is Oblada's version, apart from Chromium's |
| `patches/` | Oblada's edits to Chromium files, one patch per file (see [PATCHES.md](../PATCHES.md)) |
| `overrides/` | Oblada's logo files, which replace Chromium's at the same paths in the checkout (made by `branding/export.sh`) |
| `scripts/` | `sync.sh`, `deps.py`, `build.sh`, `patches.sh`, `sparkle.sh`, `smoke.mjs`, `updates.mjs`, `screenshot.mjs`, `dmg.sh`, `appcast.py`, `release.sh`, `symbolicate.py` |

The checkout lives in `~/chromium` (override with `OBLADA_CHROMIUM`). It takes about 32 GB of
sources and toolchains, plus about 11 GB per build directory (without debug symbols, as
`args.gn` has it).

The repo's `Makefile` runs these scripts (`make` lists its commands):

```sh
make sync       # chromium/scripts/sync.sh: check out CHROMIUM_VERSION, apply patches (re-run after bumping it)
make build      # build.sh: build Oblada.app (in out/Oblada, or the checkout's existing build directory)
make build RELEASE=1   # build.sh --release: the release build, an official one, in out/Release (hours)
make patches    # patches.sh update: after editing Chromium files in ~/chromium/src
make test       # smoke.mjs: check the patches in the build (headless, no window)
make test-updates   # updates.mjs: Oblada updating itself, end to end (headless, a test key)
make screenshot PNG=out.png   # screenshot.mjs: the window, browser UI included, headless
```

`smoke.mjs` also installs uBlock Origin and Multi-Account Containers from
addons.mozilla.org; `OBLADA_SMOKE_NETWORK=0` skips that. `screenshot.mjs` writes
`out.layout.txt` next to the image: every view of the window and where it is, plus the
window controls (which macOS draws, and the screenshot marks). `--compact` shows the
compact sidebar; `--do="39001 click:SitePanelButton"` runs browser commands and clicks
views first (a bubble it opens is saved as `out.popup.png`).

Oblada's own extension APIs (Firefox's `contextualIdentities`, for Firefox add-ons) are
built from `oblada/common/extensions/api` and registered next to Chrome's.

Releases are on GitHub, with a DMG to download (DMGs are never committed), and what
installed apps update themselves from:

```sh
make update-key                    # once: the key releases are signed with, in your keychain
make import-update-key FILE=<file> # that key from its backup (generate_keys -x), e.g. on another Mac
make dmg                           # dmg.sh: Oblada-silicon.dmg (and the .aar), in the build directory
make bump TO=0.1.1                 # raise chromium/oblada/VERSION, then commit and push it
make release NOTES=/tmp/notes.md   # release.sh: build (out/Release), pack and publish the commit
make symbolicate IPS=<report.ips>  # symbolicate.py: Oblada's functions in a release's crash report
```

Releases are official builds, as Chromium wants for any build that ships to users:
`release.sh` builds them in `out/Release`, with `args.release.gn` added to `args.gn`. That
turns on PGO with Google's profiles for the pinned Chromium (`sync.sh` fetches them:
`checkout_pgo_profiles` in `.gclient`), ThinLTO with optimizations, and stripped binaries
whose dSYMs are made as they're linked. The first build there takes hours (about 4 on an
M4), the next ones a minute or two when few files changed, thanks to the ThinLTO cache.
Changes are still built and tested in the development build directory, whose CHECK failures
say what failed.
`release.sh` keeps the dSYMs of the app's binaries on the maintainer's Mac, in
`~/chromium/symbols/Oblada-<version>-arm64-dSYMs.aar` (they're never published), and
`symbolicate.py` names Oblada's functions in macOS's crash reports of a release with them.

Oblada updates itself with [Sparkle](https://sparkle-project.org)
(`oblada/browser/updater`), which `sparkle.sh` puts in the checkout as
`third_party/sparkle`. The app in the DMG reads the feed of the latest release,
`appcast.xml`, and downloads the same app as an Apple Archive (`.aar`); both are signed
with the key `make update-key` keeps in your login keychain (back it up: without it,
installed apps can't be updated). `PUBLIC_KEY` in `oblada/browser/updater` is its public
half, which `dmg.sh` puts in the app. Builds don't update themselves: only `dmg.sh` turns
the updater on.

`release.sh` wants the commit pushed and nothing uncommitted, in the repo or in the
checkout's Chromium files. It marks the release as the latest, where the website's
download button leads: every release's DMG is `Oblada-silicon.dmg`, so
`releases/latest/download/Oblada-silicon.dmg` is always the latest one. Releases are
named and tagged after Oblada's version
(`oblada/VERSION`, e.g. `0.1.1`): raise it for each one; the notes say which Chromium it's
built on. The app
in the DMG is signed ad hoc, as a whole: there's no Developer ID yet, so macOS asks to
"Open Anyway" the first time. `node branding/dmg/render.mjs` redraws the DMG's background
from `branding/dmg/background.html` (`--mock <dir>` also shows it with Finder's icons).

Updating Chromium: change `CHROMIUM_VERSION`, run `sync.sh`, fix any patch that no longer
applies, then `patches.sh update`. The build directory is rebuilt almost whole (~6 hours),
and the checkout needs some 10 GB free for the new sources and the build: `sync.sh` keeps
each dependency one commit deep (`deps.py`), as gclient alone would fetch their whole
histories. [PATCHES.md](../PATCHES.md) says what each patch is for
and which ones depend on each other; keep it up to date when patches change.
