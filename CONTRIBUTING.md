# Contributing to Oblada

Thanks for wanting to help. Oblada is a Chromium fork laid out like Brave: this repo holds
only Oblada's own code and its patches to Chromium's files, never Chromium itself. Read
[README.md](README.md) for what Oblada is, [chromium/README.md](chromium/README.md) for
the build, and [PATCHES.md](PATCHES.md) for every patch to Chromium.

Bugs and ideas go in the [issues](https://github.com/Oblada-Browser/oblada/issues). For a
bug, say what you did, what you expected and what happened, and paste the first line of
`oblada://version` (Oblada's version and the Chromium it's based on).

## What you need

- A Mac with Apple silicon, and Xcode.
- About 70 GB free: some 32 GB of Chromium's sources and toolchains, and 11 GB for the
  build (Oblada builds without debug symbols), but it's best to have more available.
- Node.js 22 or later, for the tests; Python 3.
- Time for the first build: several hours. Later ones take a few minutes.

Oblada builds on macOS only for now, but Linux and Windows builds are planned (see
[Keep it portable](#keep-it-portable)).

## Getting started

```sh
make sync        # check out Chromium in ~/chromium (OBLADA_CHROMIUM to change it) and apply the patches
make build       # build Oblada.app
make run-fresh   # open it with a new throwaway profile, as after installing it
```

`make` alone lists every command. They run the scripts in `chromium/scripts/`, which say
what they do at the top.

## Making a change

**Oblada's code lives in `chromium/oblada/`** (namespace `oblada`), linked into the
checkout as `src/oblada`. New files go there, listed in `oblada/browser/sources.gni`;
never into Chromium's tree.

**Chromium's files get as little as possible**: the hooks that call into Oblada's code,
and what can't be done from outside. Every patch is one more thing to fix when Chromium
moves on.

1. Edit the files in `~/chromium/src`, marking Oblada's lines with `// Oblada`.
2. Run `make patches`: it rewrites `chromium/patches/`, one patch per Chromium file.
3. Update the patch's entry in [PATCHES.md](PATCHES.md), or add one: what it does, what
   it needs, and the Oblada code it calls.

A few rules:

- **Strings** Oblada shows live in `oblada/browser/l10n.cc`, in English and Italian:
  add both. Chromium's own strings are renamed "Chromium" → "Oblada" in every language by
  `chromium/scripts/rebrand.py` at build time; don't edit `*chromium_strings*` files by
  hand.
- **Prefs** are named `oblada.*`. Internal pages are shown as `oblada://` (they're
  `chrome://` underneath).
- **Code style** is Chromium's. Comments are short sentences saying what something is
  for, written for someone reading the code cold, not the story of the change.

### Keep it portable

Write features for every platform, not only macOS. Put what depends on the system in
`_mac.mm`, `_win.cc` and `_linux.cc` files behind a small interface, listed per platform
in `sources.gni`, and make strings platform-aware (⌘ or Ctrl). Say so in your pull
request when code for another platform couldn't be built or tested.

## Testing

Tests run Oblada headless, with a throwaway profile: they never show a window, nor take
the focus away from what you're doing.

```sh
make test                          # the smoke test: about 40 checks, a few minutes
make test NETWORK=0                # the same without addons.mozilla.org
make test-updates                  # Oblada updating itself, end to end, with a test key
make screenshot PNG=/tmp/shot.png  # the window, browser UI included
```

`make screenshot` also writes every view of the window and where it is
(`shot.layout.txt`), and can act first: `DO="39001 wait:500 click:SitePanelButton"`
focuses the address bar and opens the site panel (the actions are listed in
`oblada/browser/debug/window_snapshots.cc`). A change you can see deserves a look this
way, and a new behavior deserves a check in `chromium/scripts/smoke.mjs`.

## Changelog and devlog

Each change is written down as it lands, for the next release:

- in its **devlog**, `devlogs/<x_y_z>.md`: what changed and how, with the files and names
  involved, for contributors;
- if people using Oblada would notice it, in its **changelog**,
  `changelogs/<x_y_z>.json`: one plain sentence in `added`, `changed`, `removed`,
  `fixed`, `knownIssues` or `notes`. [changelogs/README.md](changelogs/README.md) has the
  format; the website shows these.

Both are in English.

## Pull requests

One topic per pull request, saying what it changes and why, with a screenshot when it
shows. Before opening it:

- [ ] `make build` passes, and `make test` too;
- [ ] `make patches` leaves `chromium/patches/` unchanged, and `PATCHES.md` matches;
- [ ] new strings are in English and Italian;
- [ ] the devlog, and the changelog if users will notice, say what changed.

Releases are cut by the maintainers (`make bump`, `make release`; see
[chromium/README.md](chromium/README.md)).

## License

Oblada is under the GNU GPL v3 (see [LICENSE](LICENSE)); contributions are too.
Chromium's own code keeps its licenses.
