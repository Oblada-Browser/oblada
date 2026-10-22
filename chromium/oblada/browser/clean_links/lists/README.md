# Firefox's lists of tracking parameters

The parameters sites add to links to track who follows them, which "Copy Clean
Link" removes (`../clean_links.h`). They are Firefox's strip-on-share lists,
unchanged, from `toolkit/components/antitracking/StripOnShareLists/` in
[Firefox's source](https://github.com/mozilla-firefox/firefox), as of revision
`d6fd7a4e69b3` (2026-10-22):

- `MPL2/StripOnShare.json`, Mozilla's, under the
  [Mozilla Public License 2.0](https://mozilla.org/MPL/2.0/);
- `LGPL/StripOnShareLGPL.json`, under the GNU Lesser General Public License 3.0
  (`LGPL/LICENSE`).

`mozilla/query-stripping.json` is Mozilla's list, which Firefox strips from
pages' addresses as they load and adds to the global rule above: the records of
the `query-stripping` collection of Firefox's Remote Settings, as its server
answered on 2026-10-22 (sorted by id), from
`https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/query-stripping/records`.
Oblada keeps it up to date from there as it runs (`../mozilla_list.h`); this
copy is used until it has downloaded one.

Each list has a `global` rule, for every site, and rules for some sites, by host
(`hosts`) or by registrable domain (`schemelessSites`); Oblada combines them as
Firefox does. To update them, copy the two files from a newer revision, and
Mozilla's list from its server, and note it here: the build reads them (`../../sources.gni` writes them into
`oblada/clean_links_lists.h`).
