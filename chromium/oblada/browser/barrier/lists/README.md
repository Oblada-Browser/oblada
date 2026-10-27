# Brave's lists for the Barrier

Data the Barrier (`../barrier.h`) works from, as Brave publishes it, under the
[Mozilla Public License 2.0](https://mozilla.org/MPL/2.0/). The build reads
them (`../../sources.gni` writes them into `oblada/barrier_lists.h`); Oblada
keeps them up to date from the same addresses as it runs (the catalog with the
filter lists, `../filter_lists.h`; the others daily, `../data_lists.h`), and
uses these copies until it has downloaded newer ones.

- `list_catalog.json`: the catalog of filter lists Brave offers
  (`filter_lists/list_catalog.json` in
  [brave/adblock-resources](https://github.com/brave/adblock-resources)), as of
  2026-10-26, without the fields for Brave's component updater
  (`list_text_component`, `component_id`, `base64_public_key`). Each list's
  sources are downloaded from their own addresses (EasyList, uBlock Origin's
  lists, Brave's…), under their own licenses.
- From `brave-lists/` in [brave/adblock-lists](https://github.com/brave/adblock-lists),
  at revision `ff303c3097335fc3bb913439e2a9cef0d0d686bb` (2026-10-21):
  - `query-filter.json`: the tracking parameters stripped from addresses
    (`../url_cleaning.h`);
  - `debounce.json`: tracking redirects, skipped for where they lead
    (`../url_cleaning.h`);
  - `request-otr.json`: sensitive sites, offered a temporary container
    (`../sensitive_sites.h`);
  - `webcompat-exceptions.json`: sites fingerprinting protection leaves some
    APIs alone on, to keep working (`../farbling.h`);
  - `https-upgrade-exceptions-list.txt`: sites not upgraded to HTTPS, which
    they serve badly (`../barrier.h`, `HttpsUpgradeFor()`).

To update them, download them again and note the date or revision here.
