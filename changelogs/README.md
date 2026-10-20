# Changelogs

Oblada's public release notes, one JSON file per version, for the website to read when
it's generated (as [Zen's release notes](https://zen-browser.app/release-notes/)). The
details for contributors are in [`devlogs/`](../devlogs).

## Files

`<major>_<minor>_<patch>.json`: `0_1_1.json` is Oblada 0.1.1. Only the `.json` files are
release notes; this README isn't one.

The next release's file is written as changes land, and kept up to date until the
release: until then it says `"released": false`.

## Format

```ts
type Changelog = {
  obladaVersion: string;    // Oblada's version, "0.1.1" (chromium/oblada/VERSION)
  releaseId: number;        // one more for each release: 0.1.0 is 1, 0.1.1 is 2...
  chromiumVersion: string;  // the Chromium it's based on, "155.0.8059.85"
  releaseDate: string;      // "YYYY-MM-DD"; "" until it's released
  released: boolean;        // false while the release is in the works

  // Optional: each a list of short sentences for users.
  added?: string[];         // what's new
  changed?: string[];       // what works differently
  removed?: string[];       // what's gone
  fixed?: string[];         // bugs that are gone
  knownIssues?: string[];   // problems still there
  notes?: string[];         // anything else worth knowing
};
```

A list with nothing in it is left out rather than empty.

`releaseId` orders the releases and never repeats: 0.1.0 is 1, 0.1.1 is 2, 0.1.2 is 3,
0.2.0 is 4, and so on, whatever the version numbers do.

On release day, `released` becomes `true` and `releaseDate` gets the date, so the website
can leave out a version still in the works.

## Writing the items

- In English, for people who use Oblada, not for those who build it: what they'll see,
  not how it's done.
- One plain sentence each, without a trailing list or a link: "Clicking the address bar
  shows the whole address, not just the site."
- Each change once, in the list it fits best.

## Example

```json
{
  "added": [
    "A downloads button at the bottom of the sidebar, next to your workspaces."
  ],
  "fixed": [
    "The site's icon could stay stuck above the address bar after hovering it."
  ],
  "knownIssues": [
    "Oblada doesn't update itself yet: new versions are on GitHub."
  ],
  "obladaVersion": "0.1.1",
  "releaseId": 2,
  "chromiumVersion": "155.0.8059.85",
  "releaseDate": "",
  "released": false
}
```
