# Oblada

A Chromium browser with **Zen's design** (tabs in a sidebar, workspaces you swipe between, compact mode, split view), **Firefox's containers** (separate cookies, logins and storage per container, with "always open this site in…" rules) and **extensions from both the Chrome Web Store and Firefox Add-ons** (including Firefox's `contextualIdentities` API, for add-ons such as Multi-Account Containers).

Oblada is a Chromium fork laid out like Brave: Chromium itself is never committed here, only Oblada's own code and its patches to Chromium's files. See [chromium/README.md](chromium/README.md) for how to check out, build and test it, [PATCHES.md](PATCHES.md) for every patch to Chromium: what it does, and what it depends on, and [CONTRIBUTING.md](CONTRIBUTING.md) to help.

```sh
make          # list the commands
make sync     # check out the pinned Chromium release and apply the patches
make build    # build Oblada.app
make test     # check the build (headless: no window, no focus taken)
make release NOTES=/tmp/notes.md   # publish the commit on GitHub as chromium/oblada/VERSION, with its DMG
```

| Path | What it is |
| --- | --- |
| `chromium/` | the browser: `oblada/` (Oblada's code), `patches/` (its edits to Chromium, described in [PATCHES.md](PATCHES.md)), `scripts/` |
| `changelogs/` | release notes: what changed in each version, as JSON for the website (`0_1_1.json`…) |
| `devlogs/` | the same in detail, for contributors (`0_1_1.md`…) |
| `branding/` | the logo, the tail of an occhiata (*Oblada melanura*): `logo.py` draws it, `export.sh` makes the browser's icons from it; `dmg/` is the background of the DMG's window |

## License

GPL-3.0 (see `LICENSE`). Chromium's own code keeps its licenses.
