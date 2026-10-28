# Oblada's patches to Chromium

Oblada changes Chromium's own files as little as it can. Its features live in
[`chromium/oblada/`](chromium/oblada) (namespace `oblada`), and Chromium's files only get
the hooks that call into that code, plus the changes that can't be made from outside them.
Those edits are kept in [`chromium/patches/`](chromium/patches), one patch per file. The
patch is named after the file's path, with `/` turned into `-`
(`chrome/browser/ui/BUILD.gn` → `chrome-browser-ui-BUILD.gn.patch`). Oblada's lines in
Chromium's files are marked with `// Oblada` comments.

```sh
chromium/scripts/patches.sh apply    # apply what isn't applied yet (build.sh and sync.sh do it)
chromium/scripts/patches.sh update   # rewrite chromium/patches/ from the edits in ~/chromium/src
chromium/scripts/patches.sh reset    # revert every patched file (before moving to a new Chromium)
```

Two kinds of changes are not patches:
- **Renaming:** "Chromium" becomes "Oblada" in every translation by
  `chromium/scripts/rebrand.py` at build time, except where the Chromium project itself is
  meant ("made possible by the Chromium open source project").
- **Logo images:** they are copied over Chromium's from `chromium/overrides/`, which
  `branding/export.sh` makes.

There are 251 patches for Chromium 156 (`chromium/CHROMIUM_VERSION`). This file describes
each one, and how they depend on each other and on Oblada's code. Update it when a patch
is added, removed or changes purpose.

## How they depend on each other

**Everything that includes `oblada/` code needs the build patch.**
[`chrome-browser-ui-BUILD.gn.patch`](#chromebrowseruibuildgn) compiles Oblada's code into
Chromium's `//chrome/browser/ui` target (sources from `chromium/oblada/browser/sources.gni`).
Every patch whose entry lists Oblada code links against it. Oblada's extension APIs have two
build patches of their own:
- `chrome/common/extensions/BUILD.gn` for the schemas and features;
- `chrome/browser/extensions/BUILD.gn` for the functions.

**A `.cc` patch needs its `.h` patch** when there is one: the header declares what the
source adds. Each pair is listed in the entries.

**Patches that need another file's patch to compile:**

| Patch | Needs | For |
| --- | --- | --- |
| `content/browser/renderer_host/render_widget_host_view_mac.mm` | `content/public/browser/web_contents_delegate.h` (and `.cc`) | `WebContentsDelegate::HandleForceClick()` |
| `chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h` | `content/public/browser/web_contents_delegate.h` | overrides `HandleForceClick()` |
| `chrome/browser/extensions/api/tabs/tabs_api.cc` | `chrome/common/extensions/api/tabs.json` | the `cookieStoreId` fields |
| `chrome/browser/extensions/extension_tab_util.cc` | `chrome/common/extensions/api/tabs.json` | `Tab.cookieStoreId` |
| `chrome/browser/extensions/chrome_extensions_browser_api_provider.cc` | `chrome/browser/extensions/BUILD.gn` | the generated function registry |
| `chrome/common/extensions/chrome_extensions_api_provider.cc` | `chrome/common/extensions/BUILD.gn` | the generated schemas and features |
| `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h` | `SetObladaShowingHeaders()` |
| `chrome/browser/ui/views/tabs/common/tab_group_header_view.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h` | `IsObladaShowingHeaders()`, `RegisterOnObladaShowingHeadersChanged()` |
| `chrome/browser/resources/settings/settings_main/settings_main.html` | `chrome/browser/resources/settings/route.ts`, `router.ts` | `routes.OBLADA` |
| `chrome/browser/resources/settings/settings_main/settings_main.ts` | `chrome/browser/resources/settings/BUILD.gn` | the `/oblada/oblada_page.js` import, known to the compiler, left out of the bundle |
| `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h` | `chrome/browser/ui/tabs/vertical_tab_strip_state.h` | `kVerticalTabStripMinUncollapsedWidth` |
| `chrome/browser/ui/tabs/vertical_tab_strip_state_controller_impl.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state.h` | `kVerticalTabStripMinUncollapsedWidth` |
| `chrome/browser/ui/views/tabs/common/tab_view.cc` | `chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h` (and `.cc`) | `CloseTabOrSelection()` |
| `chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h` | `chrome/browser/ui/views/tabs/dragging/tab_drag_target.h` | overrides `GetInitiallyActiveTab()` |
| `chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.cc` | `tab_drag_target.h`, `tab_drag_controller.h` and `.cc` | `GetInitiallyActiveTab()` (its default definition is in `tab_drag_controller.cc`) |
| `components/safe_browsing/core/browser/db/sb_protocol_config.cc` | `components/safe_browsing/core/browser/db/BUILD.gn` | `oblada/components/safe_browsing/buildflags.h` |
| Blink's fingerprinting hooks (`canvas`, `webaudio`, `navigator_*`, `screen.cc`…) | `third_party/blink/renderer/core/execution_context/execution_context.h`, `third_party/blink/renderer/core/BUILD.gn` | `GetObladaFarbling()`, which `oblada/blink/farbling.h` reads, compiled into Blink's core |
| `third_party/blink/renderer/core/frame/web_local_frame_impl.h` (and `.cc`) | `third_party/blink/public/web/web_local_frame.h` | overrides `SetObladaFarbling()` |
| `third_party/blink/renderer/core/workers/dedicated_worker.cc`, `worker_global_scope.cc`, `worklet_global_scope.cc`, `threaded_worklet_messaging_proxy.cc`, `paint_worklet_global_scope_proxy.cc`, `layout_worklet_global_scope_proxy.cc` | `third_party/blink/renderer/core/workers/global_scope_creation_params.h` | `oblada_farbling` |
| `shared_worker_content_settings_proxy.{h,cc}` (Blink's and content's), `service_worker_content_settings_proxy.{h,cc}` (Blink's and content's), `worker_global_scope.cc` | `third_party/blink/public/mojom/worker/worker_content_settings_proxy.mojom`, `third_party/blink/public/platform/web_content_settings_client.h` | `GetObladaFarbling()` |
| content's `shared_worker_content_settings_proxy_impl.cc`, `service_worker_content_settings_proxy_impl.cc`; `chrome/browser/chrome_content_browser_client.{h,cc}` | `content/public/browser/content_browser_client.h` | `GetObladaWorkerFarbling()` |
| `third_party/blink/renderer/modules/websockets/websocket_channel_impl.{h,cc}`, `modules/eventsource/event_source.{h,cc}` | `third_party/blink/renderer/core/BUILD.gn` | `oblada/blink/connection_pools.h`, compiled into Blink's core |
| `net/url_request/url_request_job.cc`, `net/cookies/canonical_cookie.cc`, `third_party/blink/renderer/platform/weborigin/security_policy.cc` | `net/base/features.h` (and `.cc`) | `kObladaReferrerCap`, `kObladaCookieExpiryCap` |
| `chrome/renderer/chrome_content_renderer_client.cc` | `chrome/renderer/BUILD.gn` | `//oblada/renderer/barrier` |

**Oblada code that needs a patch to compile.**
- `chromium/oblada/browser/workspaces/workspace_slider.cc` calls
  `TabCollectionAnimatingLayoutManager::SnapToTarget()`, which the
  `tab_collection_animating_layout_manager` patches add.
- The Barrier's agent (`oblada/renderer/barrier`) calls `WebLocalFrame::SetObladaFarbling()`
  (`web_local_frame.h`) and runs in `ISOLATED_WORLD_ID_OBLADA_BARRIER`
  (`chrome_isolated_world_ids.h`); its engine's bridge (`oblada/components/barrier/adblock`)
  needs the `adblock` crate (the `third_party/rust` patches).

**Patches that only work together**, though each compiles alone:

- **Containers in sessions:** `session_service_base.cc` and `browser_live_tab_context.cc` save
  a tab's container (under `oblada.container`); `browser_tabrestore.cc` reads it back.
- **`oblada://`:**
  - `url_fixer.cc` maps a typed `oblada://` to `chrome://`;
  - `url_formatter.cc` shows `chrome://` as `oblada://`;
  - `chrome_autocomplete_scheme_classifier.cc` treats the typed text as a URL;
  - `builtin_provider.cc` suggests the pages;
  - the `chrome_urls` page lists them.
- **Vertical tabs, but in the layouts with horizontal ones:** `tab_strip_prefs.cc` and
  `vertical_tab_strip_state_controller_impl.cc` turn them on, the second as the window's
  layout says (`oblada::sidebar::HasHorizontalTabs()`). These patches remove every other way
  to switch:
  - `browser_command_controller.cc`;
  - `main_menu_builder.mm`;
  - `app_menu_model.cc`;
  - `action_app_menu_manager.cc`;
  - `system_menu_model_builder.cc`;
  - `tab_menu_model.cc`.
- **The sidebar layout:** `browser_view_tabbed_layout_impl.cc` puts the toolbar in the
  sidebar (through `oblada::sidebar::SetToolbarInSidebar()`). That is what `toolbar_view.cc`,
  `vertical_tab_strip_region_view.cc` and `browser_native_widget_mac.mm` react to.
- **The window's layouts** (sidebar, scaffold, expanded scaffold, compact, and those with
  horizontal tabs: `oblada/browser/ui/sidebar_mode.h`):
  `vertical_tab_strip_state_controller_impl.cc` starts each window collapsed or not as the
  layout says, `vertical_tab_strip_region_view.cc` keeps the two in step
  (`oblada::sidebar::ModeKeeper`), `browser_view_tabbed_layout_impl.cc` lays each out,
  `toolbar_view.cc` drops its extra buttons in scaffold modes, and `app_menu_model.cc` and
  Settings > Oblada pick one.
- **Horizontal tabs** (horizon, horizon compact, classic: `oblada/browser/ui/horizontal_tabs.h`):
  `vertical_tab_strip_state_controller_impl.cc` turns them on, `browser_view_tabbed_layout_impl.cc`
  lays out their rows and marks the tab strip's region and the toolbar with the layout
  (`oblada::horizontal::SetLayout()`), which these follow:
  - `horizontal_tab_strip_region_view.{h,cc}` (the workspace's pill, no tab search);
  - `tab.{h,cc}` (widths, close button, centered titles, containers' colors);
  - `horizontal_tab_style_views.cc` (the tabs' veils);
  - `tab_container_impl.cc` and `tab_group_header.cc` (no workspace headers);
  - `toolbar_view.cc` (the address bar centered or over the active tab, the media button);
  - `tab_hover_card_controller.cc` (hover cards);
  - `browser_native_widget_mac.mm` (the glass over the whole window).
- **The compact sidebar's card:** `browser_view.cc` makes Chromium's blur backdrop also
  without glass, `browser_view_tabbed_layout_impl.cc` lays it out and sets it up as the card
  (`oblada::sidebar::SetCard()`), and `vertical_tab_strip_background_blur_backdrop.cc` draws
  it.
- **The workspace's header, on demand:** `vertical_tab_strip_region_view.cc` tells whether
  the sidebar shows it, `tab_group_header_view.cc` follows, and `tab_group_view_layout.cc`
  leaves no room for it when hidden. Settings > Oblada turns it on.
- **Settings > Oblada:** `settings_ui.cc` serves the page (`oblada/browser/settings/
  oblada_page.h`), `settings_main.ts` imports it (which `BUILD.gn` lets compile and leaves
  out of the bundle), `route.ts` and `router.ts` route to it, `settings_main.html` shows it,
  `settings_menu.html` lists it, and `prefs_util.cc` lets it change its preferences.
- **The floating address bar:** `location_bar_view.cc` makes the bar float. These patches
  adapt to it:
  - `top_container_view.cc`;
  - `browser_view_layout_impl.cc`;
  - `omnibox_view_views.cc`;
  - `omnibox_popup_presenter_base.cc`;
  - `omnibox_popup_webui_base_content.cc`;
  - `rounded_omnibox_results_frame.cc`;
  - `vertical_tab_strip_region_view.cc`.
- **Workspaces:** they are tab groups shown in Chromium's "focus" mode.
  - `ui_features.cc` turns that mode on, which every workspace patch relies on.
  - These patches keep the groups local and out of the bookmarks bar:
    - `saved_tab_group_model_listener.cc`;
    - `tab_group_sync_delegate_desktop.cc`;
    - `session_restore.cc`;
    - `components/saved_tab_groups/public/pref_names.cc`;
    - `bookmark_utils.cc`.
- **Split view:**
  - `browser_view_tabbed_layout_impl.cc` leaves the band above the panes where
    `multi_contents_view.cc` puts its grips;
  - `contents_container_view.cc` hides the mini toolbar, which `contents_container_outline.cc`
    then leaves no notch for;
  - `split_tab_view.cc` lays the split's tabs side by side, and
    `tab_view_vertical_layout.cc` fits them.
- **Full screen on macOS:** these work together:
  - `window_feature_controller.cc`;
  - `fullscreen_util_mac.cc`;
  - `browser_frame_view_mac.mm`;
  - `browser_view_layout_delegate_impl.cc`;
  - `browser_native_widget_mac.mm`.
- **Updates:** `chrome/BUILD.gn` puts Sparkle in the app, which Oblada's updater
  (`oblada/browser/updater`) loads; `about_handler.cc` shows what the updater does, and
  `application_lifetime_desktop.cc` turns the About page's Relaunch into installing the
  update that's ready.
- **The transparent New Tab page:**
  - `search.cc` points the New Tab page to it;
  - `contents_web_view.cc` lets the window show through;
  - `browser_web_contents_delegate.cc` lets it focus the address bar once loaded;
  - `chrome_web_ui_configs.cc` registers it.

## By area

| Area | Patches |
| --- | --- |
| Build | `chrome/browser/ui/BUILD.gn`, `chrome/browser/extensions/BUILD.gn`, `chrome/common/extensions/BUILD.gn` |
| Updates | `chrome/BUILD.gn`, `application_lifetime_desktop.cc`, `about_handler.cc` |
| Startup, prefs, stability | `chrome_browser_main.cc`, `browser_prefs.cc`, `session_startup_pref.cc`, `base/check.cc`, `web_ui_main_frame_observer.cc`, `startup/infobar_utils.cc` |
| Branding | `BRANDING`, `chrome_paths_mac.mm`, `keychain_password_mac.mm`, three `icons.html.ts` |
| Window & frame | `ui_base_features.cc`, `browser_native_widget_mac.mm`, `browser_frame_view_mac.mm`, `browser_view_layout_delegate_impl.cc`, `fullscreen_util_mac.cc`, `window_feature_controller.cc` |
| Sidebar & compact mode | `browser_view_tabbed_layout_impl.cc`, `browser_view.cc`, `tabs/features.cc`, `vertical_tab_strip_background_blur_backdrop.cc`, `toolbar_view.cc`, `vertical_tab_strip_region_view.{h,cc}`, `vertical_tab_strip_state.h`, `vertical_tab_strip_state_controller{.h,_impl.cc}`, `session_service_base.cc`, `browser_window_features.cc`, `tab_strip_prefs.cc`, `browser_command_controller.cc`, `vertical_tab_strip_bottom_container.cc`, `download_toolbar_ui_controller.cc` |
| Horizontal tabs | `horizontal_tab_strip_region_view.{h,cc}`, `tab.{h,cc}`, `new_tab_button.{h,cc}`, `horizontal_tab_style_views.cc`, `tab_container_impl.cc`, `tab_group_header.cc` |
| Tabs | `tab_style.cc`, `tab_hover_card_controller.cc`, `tab_strip_collection_controller.{h,cc}` |
| Workspaces | `ui_features.cc`, `browser_commands.cc`, `browser_widget.cc`, `tab_group_header_view.{h,cc}`, `tab_group_view_layout.cc`, `tab_collection_animating_layout_manager.{h,cc}`, `vertical_tab_strip_focus_swipe_controller.cc`, `saved_tab_group_model_listener.cc`, `tab_group_sync_delegate_desktop.cc`, `session_restore.cc`, `saved_tab_groups/public/pref_names.cc`, `bookmark_utils.cc` |
| Containers | `browser_web_contents_delegate.cc` (no prerendering), `search_prefetch_service.cc`, `chrome_content_browser_client.cc`, `chrome_content_browser_client_navigation_throttles.cc`, `tab_helpers.cc`, `browser_navigator.cc`, `web_contents_impl.cc`, `session_service_base.cc`, `browser_live_tab_context.cc`, `browser_tabrestore.cc`, `tab_lifecycle_unit.cc`, `render_view_context_menu.{h,cc}`, `new_tab_button.{h,cc}`, `new_tab_button_menu_model.{h,cc}`, `tab_view.cc` |
| Address bar | `location_bar_view.{h,cc}`, `top_container_view.cc`, `browser_view_layout_impl.cc`, `omnibox_view_views.cc`, `omnibox_popup_presenter_base.cc`, `omnibox_popup_webui_base_content.cc`, `rounded_omnibox_results_frame.cc`, `location_bar_model_delegate.cc`, `aim_eligibility_service_features.cc` |
| Split view | `multi_contents_view.{h,cc}`, `multi_contents_drop_target_view.cc`, `multi_contents_view_drop_target_controller.{h,cc}`, `multi_contents_resize_area.h`, `contents_container_view.cc`, `contents_container_outline.{h,cc}`, `split_tab_view.{h,cc}`, `tab_view_vertical_layout.cc`, `tab_drag_controller.{h,cc}`, `tab_drag_target.h` |
| Peek | `web_contents_delegate.{h,cc}` (content), `render_widget_host_view_mac.mm`, `browser_web_contents_delegate.{h,cc}`, `tab_helpers.h` |
| Clean links | `omnibox_context_menu_mixin.cc`, `link_to_text_menu_observer.cc`, `render_view_context_menu.cc`, `browser_commands.cc` |
| Safe Browsing | `safe_browsing/core/browser/db/BUILD.gn`, `sb_protocol_config.cc` |
| Barrier | `chrome_content_browser_client.cc`, `location_bar_view.cc`, `render_view_context_menu.cc`, `browser_prefs.cc`, `settings_ui.cc`, `route.ts`, `router.ts`, `settings_main.html`, `prefs_util.cc`, `chrome_content_browser_client_navigation_throttles.cc`, `chrome_browser_interface_binders.cc`, `tab_helpers.cc`, `chrome_content_renderer_client.cc`, `chrome/renderer/BUILD.gn`, `chrome_isolated_world_ids.h`, `chrome_feature_list_creator.cc`, `renderer_preferences_util.cc`, `pref_watcher.cc`, `render_view_host_impl.cc`, `https_upgrades_interceptor.cc`, `https_upgrades_util.cc`, `net/base/features.{h,cc}`, `transport_security_state.{h,cc}`, `url_request_http_job.cc`, `url_request_job.cc`, `canonical_cookie.cc`, `security_policy.cc`, `content_browser_client.h`, `chrome_content_browser_client.h`, the worker settings' proxies in content (4), Blink's fingerprinting protection (46), the Rust crates' (`Cargo.toml`, `Cargo.lock`, `gnrt_config.toml`, 10 `BUILD.gn`/`README.chromium`) |
| Performance | `background_tab_loading_policy.cc`, `user_tuning/prefs.cc`, `download_features.cc`, `history_clusters/core/features.cc`, `chrome_browser_main_extra_parts_performance_manager.cc`, `soda_installer.cc`, `wasm_tts_engine_component_installer.{h,cc}`, `read_anything_service.cc`, `soda_component_installer.h`, `soda_language_pack_component_installer.h`, `screen_ai_component_installer.h` |
| New Tab & welcome | `search.cc`, `contents_web_view.{h,cc}`, `startup_browser_creator_impl.cc` |
| Extensions & Firefox add-ons | `chrome_download_manager_delegate.cc`, `cookies_api.cc`, `tabs_api.{h,cc}`, `tabs.json`, `extension_tab_util.cc`, `chrome_extensions_browser_api_provider.cc`, `chrome_extensions_api_provider.cc`, `manifest_v2_handler.cc`, `extensions_toolbar_desktop.cc` |
| Menus | `app_menu_model.cc`, `action_app_menu_manager.cc`, `app_menu.cc`, `browser_app_menu_button.cc`, `tab_menu_model.{h,cc}`, `main_menu_builder.mm`, `system_menu_model_builder.cc`, `system_menu_model_delegate.cc` |
| URLs & search | `url_fixer.cc`, `url_formatter.cc`, `chrome_autocomplete_scheme_classifier.cc`, `builtin_provider.cc`, `template_url_prepopulate_data.cc` |
| Internal pages | `chrome_web_ui_configs.cc`, `settings_ui.cc`, `page_visibility.ts`, `appearance_page.html.ts`, `settings_main.{html,ts}`, `settings_menu.html`, settings' `BUILD.gn`, `route.ts`, `router.ts`, `prefs_util.cc`, `about_handler.cc`, `chrome_pages.cc`, `version_ui.cc`, `about_version.html`, `chrome_urls/resources/app.{ts,html.ts}` |

## The patches

Each entry gives the patch, what it does, what it **needs** (other patches it doesn't compile
or work without) and the Oblada code it calls.

### Build

#### `chrome/browser/ui/BUILD.gn`
`chrome-browser-ui-BUILD.gn.patch`. Imports `//oblada/browser/sources.gni` and adds
`oblada_ui_sources` (to its desktop sources) and `oblada_ui_deps` to
`static_library("ui")`. Oblada's code is then
compiled as part of `//chrome/browser/ui`, so it can use that target's headers without a
dependency cycle.
- Needs: nothing. Every patch that includes `oblada/browser/` needs it.

#### `chrome/browser/extensions/BUILD.gn`
`chrome-browser-extensions-BUILD.gn.patch`. Adds `//oblada/browser/extensions/api:api_registration`
to the `extensions` source set: the generated registration of Oblada's extension API
functions (`contextualIdentities`).
- Needed by: `chrome_extensions_browser_api_provider.cc`.

#### `chrome/common/extensions/BUILD.gn`
`chrome-common-extensions-BUILD.gn.patch`. Adds `//oblada/common/extensions/api` to the
`extensions` source set: Oblada's extension API schemas, generated types and feature
providers.
- Needed by: `chrome/common/extensions/chrome_extensions_api_provider.cc`.

### Startup, prefs and stability

#### `chrome/browser/chrome_browser_main.cc`
`chrome-browser-chrome_browser_main.cc.patch`. Adds Oblada's `ChromeBrowserMainExtraParts`
(`oblada::CreateBrowserMainExtraParts()`). Its `PreProfileInit()` marks Chromium's first run
window (signing in to Google) as done, so that it never opens, and has the session restored
after a relaunch to update; its `PostBrowserStart()` starts the Firefox add-ons, workspaces,
peek and the updater.
- Oblada code: `oblada/browser/oblada_browser_main_extra_parts.h`.

#### `chrome/browser/prefs/browser_prefs.cc`
`chrome-browser-prefs-browser_prefs.cc.patch`. `RegisterProfilePrefs()` calls
`oblada::RegisterProfilePrefs()`, which registers the prefs of the containers, Firefox
add-ons, New Tab page and workspaces; `RegisterLocalState()` calls
`oblada::RegisterLocalStatePrefs()`, for the updater's. At the end of
`RegisterProfilePrefs()`, `oblada::RegisterProfilePrefDefaults()` changes Chromium's own
defaults, once they're registered (third-party cookies blocked, the Barrier's). New Oblada
prefs go in `oblada/browser/prefs.cc`, not here.
- Oblada code: `oblada/browser/prefs.h`.

#### `chrome/browser/prefs/session_startup_pref.cc`
`chrome-browser-prefs-session_startup_pref.cc.patch`. On startup, Oblada reopens the last
session's tabs (and so its workspaces) by default, as Zen and Arc do; Settings' "On
startup" can still change it. Needed by: `ui_features.cc` turning off the infobar that
offered it.

#### `base/check.cc`
`base-check.cc.patch`. `DUMP_WILL_BE_CHECK` failures log an error and go on in development
builds too, as in official builds (Chrome's, Oblada's releases), instead of crashing.
Non-fatal check failures no longer write crash dumps, which would have nowhere to go and
would pile up next to the real ones.

#### `content/browser/webui/web_ui_main_frame_observer.cc`
`content-browser-webui-web_ui_main_frame_observer.cc.patch`. A JavaScript error on an
internal page doesn't crash development builds either, as it would in a build that isn't an
official one (releases are).

#### `chrome/browser/ui/startup/infobar_utils.cc`
`chrome-browser-ui-startup-infobar_utils.cc.patch`. Startup infobars:
- No "Google API keys are missing": Oblada doesn't use them.
- No prompts (restoring tabs, the default browser, PDFs) over `oblada://welcome`, which has
  its own steps. Warnings (an obsolete system, dangerous flags) still show.
- Oblada code: `oblada/browser/welcome/welcome_ui.h`.

### Updates

#### `chrome/BUILD.gn`
`chrome-BUILD.gn.patch`. The app bundle gets Sparkle.framework in `Contents/Frameworks`
(`//oblada/browser/updater:sparkle_framework`), which Oblada's updater loads at startup.
`chromium/scripts/sparkle.sh` puts Sparkle in the checkout, as `third_party/sparkle`.

#### `chrome/browser/lifetime/application_lifetime_desktop.cc`
`chrome-browser-lifetime-application_lifetime_desktop.cc.patch`. A relaunch (the About
page's Relaunch button, or any other) with an update ready has the updater install it:
Sparkle replaces the app once Oblada has quit, then opens it again
(`oblada::updater::InstallAndRelaunch()`). Chromium's own relaunch would open the old app
while Sparkle replaces it. The session is restored on the next start as after Chromium's
relaunches (`oblada::updater::PrepareStartup()`).
- Oblada code: `oblada/browser/updater/updater.h`.

### Branding

#### `chrome/app/theme/chromium/BRANDING`
`chrome-app-theme-chromium-BRANDING.patch`. Company and product names "Oblada", Mac bundle
id `dev.oblada.Oblada`, creator code `Obld`. It replaces the whole file, so any upstream
change to it conflicts.

#### `chrome/common/chrome_paths_mac.mm`
`chrome-common-chrome_paths_mac.mm.patch`. The data folder is
`~/Library/Application Support/Oblada` instead of `Chromium`. Changing it moves users' data.

#### `components/os_crypt/common/keychain_password_mac.mm`
`components-os_crypt-common-keychain_password_mac.mm.patch`. The key Oblada encrypts saved
passwords and cookies with is kept in the Keychain as "Oblada Safe Storage" (account
"Oblada"), not under Chromium's names: other Chromium-based browsers use those, and macOS
asked for the user's password to let Oblada read their key. Until 0.1.1 Oblada used
Chromium's; there's no migration (the maintainer's key was copied by hand), so a profile
from then loses what it had saved encrypted.

#### `chrome/browser/resources/settings/icons.html.ts`, `chrome/browser/resources/webui_browser/icons.html.ts`, `ui/webui/resources/cr_elements/icons.html.ts`
`chrome-browser-resources-settings-icons.html.ts.patch`,
`chrome-browser-resources-webui_browser-icons.html.ts.patch`,
`ui-webui-resources-cr_elements-icons.html.ts.patch`. The `chrome-product` icon of internal
pages becomes Oblada's logo, in each file's icon sets. The paths come from
`branding/vector_icon.py --svg <size>`.

### Window and frame

#### `ui/base/ui_base_features.cc`
`ui-base-ui_base_features.cc.patch`. Turns `kGlassFrame` on: the window's glass, which the
sidebar is drawn on.

#### `chrome/browser/ui/views/frame/browser_native_widget_mac.mm`
`chrome-browser-ui-views-frame-browser_native_widget_mac.mm.patch`. macOS only.
- Lowers the glass's tint (0.22 light, 0.30 dark) so the workspace colors show.
- Has Oblada paint the tint view over the glass (`oblada::window_background::Paint()`): the
  workspace's theme, a gradient of its colors with its opacity and grain, else the frame
  color at that opacity.
- Makes the glass span the whole window, with the page floating on it, with nothing under
  it (Chromium's opaque view under the glass would cover the whole window then): with
  horizontal tabs too, which Chromium gives glass only above the page, and as lightly
  tinted.
- Centers the window controls on the sidebar's row of buttons.
- Hides "Always show toolbar in full screen", which tabbed windows don't need.
- Needs: the sidebar layout (`ToolbarInset()`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `window_background.h`.

#### `chrome/browser/ui/views/frame/browser_frame_view_mac.mm`
`chrome-browser-ui-views-frame-browser_frame_view_mac.mm.patch`. macOS only. In full screen,
the window's content starts below the menu bar that slides in at the top, instead of under
it; the window controls' bar below the menu bar shows over the content (Chromium's
`TopUIFullscreenYOffset()` has room for both).
- Works with: `browser_view_layout_delegate_impl.cc`.

#### `chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc`
`chrome-browser-ui-views-frame-layout-browser_view_layout_delegate_impl.cc.patch`. macOS
only. Lays the browser out within a client area that doesn't cover the whole view (full
screen, see above) instead of failing a `CHECK`.
- Works with: `browser_frame_view_mac.mm`.

#### `chrome/browser/ui/fullscreen_util_mac.cc`
`chrome-browser-ui-fullscreen_util_mac.cc.patch`. macOS only. Normal windows keep their
sidebar in full screen, whatever "Always show toolbar in full screen" says. App windows keep
their setting.

#### `chrome/browser/ui/window_feature_controller/window_feature_controller.cc`
`chrome-browser-ui-window_feature_controller-window_feature_controller.cc.patch`. macOS
only. Only installed web apps use immersive full screen. Tabbed windows keep their sidebar
instead of moving the toolbar into a pane at the top.

### Sidebar and compact mode

#### `chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc`
`chrome-browser-ui-views-frame-layout-browser_view_tabbed_layout_impl.cc.patch`. The core of
the Zen-like layout, and the largest patch.
- The window's layout (`oblada/browser/ui/sidebar_mode.h`, which the tab strip's region
  view carries, `oblada::sidebar::ModeOf()`):
  - **Sidebar** (vertical tabs expanded): the toolbar is laid out at the top of the tab
    column.
  - **Scaffold** (collapsed): the toolbar is a row at the top of the window, next to the
    window controls (`oblada::sidebar::SetScaffoldToolbar()`), and the narrow tab strip a
    column of the tabs' icons below it, left of the page, with the page's margins around
    it.
  - **Expanded scaffold** (expanded): laid out as scaffold, the toolbar at the top of the
    window, but with the full tab strip below it, as wide as the sidebar
    (`oblada::sidebar::IsScaffold()`), against the window's edge and the page as the
    sidebar is, so that its resize area is at the page's edge; scaffold's margins come in
    as it collapses.
  - **Compact** (collapsed): only the page. The narrow tab strip is out of the window but
    for the margin at its left edge, where the pointer expands it on hover: a card widens
    over the page from there into a panel (Chromium's expand-on-hover width), what's in it
    laid out at its full width from the start, clipped to the card (`ClipToCard()`): the
    toolbar comes down at its top, fading in (Chromium's expand-on-hover opacity), the tabs
    below it. Hidden, the toolbar is laid out just out of the window, for its address bar
    to float over the page from there. Chromium's blur backdrop is the card (`SetCard()`,
    see `vertical_tab_strip_background_blur_backdrop.cc`), opaque; the strip and the
    toolbar are see-through over it, without their outline, shadow or blur.
  - **Horizontal tabs** (`oblada/browser/ui/horizontal_tabs.h`,
    `TransientLayoutData::oblada_horizontal`), in rows at the top of the window, the tab
    strip's region and the toolbar marked with the layout (`oblada::horizontal::SetLayout()`):
    in classic mode the tabs next to the window controls and the toolbar below them; in
    horizon mode the toolbar next to the window controls and the tabs below it; in horizon
    compact mode the tabs in the toolbar's row, where its layout would put the address bar
    (`CompactTabsRoom()`). The tabs' veils are as far from the address bar as it is from the
    page. The toolbar is see-through over the glass, as in the sidebar.
- **Page:** inset by `ContentInsets()` and rounded by `ContentCornerRadius()`, floating next
  to the sidebar, or below the rows of horizontal tabs.
- **Split view:** the panes get a top band for their grips.
- **Colors:** the toolbar gets the sidebar's color; in compact mode both are see-through
  over the card.
- **Removed:** Chromium's tab-strip corners, caption exclusion and separators.
- **Window controls:** in compact mode they show in the panel's corner once its toolbar
  has come down (`oblada::sidebar::SetWindowControls()`).
- Needs: `vertical_tab_strip_state.h` (default width); `browser_view.cc` and
  `vertical_tab_strip_background_blur_backdrop.cc` for the card;
  `vertical_tab_strip_region_view.cc` for the layout (its `ModeKeeper`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `sidebar_mode.h`, `sidebar_card.h`,
  `floating_location_bar.h`, `split_handle.h`, `traffic_lights.h`.
- When rebasing, expect conflicts here first: there are many hunks across
  `CalculateProposedLayout()`, `ConfigureTopContainerBackground()`,
  `DoPreLayoutComputations()` and `DoPostLayoutVisualAdjustments()`.

#### `chrome/browser/ui/views/frame/browser_view.cc`
`chrome-browser-ui-views-frame-browser_view.cc.patch`. Makes the view behind the vertical
tab strip (`VerticalTabStripBackgroundBlurBackdrop`) also without glass (macOS 26's, the only
one Chromium makes it for): it draws the compact sidebar's card.
- Needed by: `browser_view_tabbed_layout_impl.cc` (compact mode on every system).

#### `chrome/browser/ui/views/frame/vertical_tab_strip_background_blur_backdrop.cc`
`chrome-browser-ui-views-frame-vertical_tab_strip_background_blur_backdrop.cc.patch`. Draws
the compact sidebar's card when the layout has set one (`oblada::sidebar::PaintCard()`),
the panel widening over the page, opaque with the window's theme.
- Oblada code: `oblada/browser/ui/sidebar_card.h`.

#### `chrome/browser/ui/tabs/features.cc`
`chrome-browser-ui-tabs-features.cc.patch`. The compact sidebar opens 80 ms after the
pointer stops on it (`kVerticalTabsExpandOnHoverDelay`), rather than once Chromium's
velocity heuristic finds the pointer has slowed down enough
(`kVerticalTabsExpandOnHoverUseVelocityHeuristic` off), which often took a few hundred
milliseconds.

#### `chrome/browser/ui/views/toolbar/toolbar_view.cc`
`chrome-browser-ui-views-toolbar-toolbar_view.cc.patch`. The toolbar in the sidebar, and
at the top of the window in scaffold modes and with horizontal tabs, without the profile and
extensions buttons there too (`oblada::sidebar::IsScaffoldToolbar()`).
- Two rows: the buttons next to the window controls, then the address bar's own row.
- Hidden: avatar, overflow and extensions buttons. In the sidebar, reload only when it's
  too narrow for it (`oblada::sidebar::ShowIfRoomInSidebar()`): it's in the menu then.
- Smaller button insets.
- `LayOutLocationBar()` lets the address bar float out of it.
- Always drops the split-tabs and Battery Saver buttons.
- The media button shows only with horizontal tabs: otherwise media is controlled at the
  bottom of the sidebar (`oblada::media::MediaControls`, added by the
  `vertical_tab_strip_region_view.cc` patch).
- With horizontal tabs (`oblada::horizontal::LayoutOf()`): the address bar in the middle of
  the window in horizon mode (`CenterLocationBar()`), over the active tab in horizon compact
  mode (`LayOutCompactLocationBar()`).
- Needs: the sidebar layout (which sets `ToolbarInset()`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `floating_location_bar.h`,
  `horizontal_tabs.h`.

#### `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h`, `.cc`
`chrome-browser-ui-views-frame-vertical_tab_strip_region_view.{h,cc}.patch`. The tab
strip's region in the sidebar.
- Adds the workspace switcher at the bottom of the content area (`oblada_workspaces_`),
  and the media controls above it (`oblada_media_`); the media controls turn compact with
  the strip, the switcher a column in scaffold mode (`oblada::sidebar::Expansion()`).
- Keeps the strip collapsed as the window's layout says, and the layout as the strip is
  collapsed or expanded (`oblada_mode_`, `oblada::sidebar::ModeKeeper`).
- In scaffold mode, no expanding on hover.
- Puts its resize area at its own right edge: Chromium used its right edge in its parent,
  which is the same only at the window's edge (not in scaffold mode, nor while it
  collapses into it).
- Drops Chromium's swipe controller: workspace swipes are `oblada::workspaces::WorkspaceSlider`'s.
- In the sidebar and in scaffold modes, leaves the row of buttons above the tabs out of its
  layout (`CalculateProposedLayout()`, `IsObladaSidebar()`) and hides the line under it.
- Tells the state controller whether the strip shows the workspace's header: when the
  user wants it (`oblada::workspaces::ShowsHeader()`) and the strip looks expanded.
- In compact mode, the toolbar counts as part of the strip for expand-on-hover.
- The strip collapses while the address bar floats over the page.
- The address bar's suggestions keep the compact sidebar open.
- Needs: `vertical_tab_strip_state_controller.h`, `vertical_tab_strip_state.h`.
- Oblada code: `oblada/browser/workspaces/workspace_switcher.h`, `workspaces.h`,
  `oblada/browser/ui/media_controls.h`, `oblada/browser/ui/sidebar_layout.h`,
  `sidebar_mode.h`, `floating_location_bar.h`.

#### `chrome/browser/ui/tabs/vertical_tab_strip_state.h`
`chrome-browser-ui-tabs-vertical_tab_strip_state.h.patch`. The default sidebar width goes
from 240 to 280, and `kVerticalTabStripMinUncollapsedWidth` (170) is the narrowest it gets,
so the toolbar always fits.
- Needed by: `vertical_tab_strip_state_controller_impl.cc`, `vertical_tab_strip_region_view.h`,
  `browser_view_tabbed_layout_impl.cc`.

#### `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h`, `_impl.cc`
`chrome-browser-ui-tabs-vertical_tab_strip_state_controller.h.patch` and
`chrome-browser-ui-tabs-vertical_tab_strip_state_controller_impl.cc.patch`. Since Chromium
156 the controller is an interface (`.h`) with its implementation in
`VerticalTabStripStateControllerImpl`.
- **`.h`:** adds `IsObladaShowingHeaders()`, `SetObladaShowingHeaders()` and
  `RegisterOnObladaShowingHeadersChanged()` to the interface, which track whether the strip
  shows the workspace's header.
- **`_impl.cc`:** turns vertical tabs on but in the window layouts with horizontal ones
  (`oblada::sidebar::HasHorizontalTabs()`), following the layout's pref rather than
  Chromium's, which nothing switches any more (`SetVerticalTabsEnabled()` does nothing); and
  clamps the width to the minimum (fixing narrower widths saved before). A window starts collapsed or not as its
  layout says (`oblada::sidebar::CollapsesTabStrip()`), whatever the session saved: the
  layout is the same in every window.
- Needs: `vertical_tab_strip_state.h`.
- Needed by: `vertical_tab_strip_region_view.cc`, `tab_group_header_view.cc`.

#### `chrome/browser/sessions/session_service_base.cc` (sidebar's width)
`chrome-browser-sessions-session_service_base.cc.patch`, which also keeps containers (see
below). The sidebar keeps its width across restarts: `BuildCommandsForBrowser()` writes the
vertical tab strip's state (collapsed, width) into a rebuilt session. Chromium writes it only
as it changes, so a session rebuilt since (every 250 changes) restored windows at the default
width.

#### `chrome/browser/ui/browser_window/internal/browser_window_features.cc`
`chrome-browser-ui-browser_window-internal-browser_window_features.cc.patch`. A window
restored without its sidebar's width (from a session saved before the patch above) gets the
last width used (`prefs::kVerticalTabsUncollapsedWidth`), as new windows do, rather than the
default.

#### `chrome/browser/ui/tabs/tab_strip_prefs.cc`
`chrome-browser-ui-tabs-tab_strip_prefs.cc.patch`. Vertical tabs are on by default.

#### `chrome/browser/ui/browser_command_controller.cc`
`chrome-browser-ui-browser_command_controller.cc.patch`. Disables the command that switches
to horizontal tabs, and the vertical tabs' "Send feedback", which opens Google's feedback
form.

#### `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.cc`
`chrome-browser-ui-views-tabs-vertical-vertical_tab_strip_bottom_container.cc.patch`. The
sidebar's new tab button shows its label: a Zen-like "+ New tab" row aligned with the tab
titles.

#### `chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.cc`
`chrome-browser-ui-views-download-bubble-download_toolbar_ui_controller.cc.patch`. The
downloads button is at the bottom of the sidebar, as in Zen, rather than in the toolbar.
- The progress ring, badge and bubble go to the sidebar's button, and the bubble opens
  above it.
- The toolbar's button no longer shows when a download starts, nor does the animation
  rising from the page to it.
- Oblada code: `oblada/browser/ui/downloads_button.h` (the button, which
  `oblada/browser/workspaces/workspace_switcher.h` puts before the workspaces).

### Horizontal tabs

The window layouts with horizontal tabs (`oblada/browser/ui/horizontal_tabs.h`): Chromium's
horizontal tab strip, which only they show, made as plain as the sidebar. The browser layout
(`browser_view_tabbed_layout_impl.cc`, see Sidebar and compact mode) lays them out and marks
the strip's region with the layout (`oblada::horizontal::LayoutOf()`).

#### `chrome/browser/ui/views/frame/horizontal_tab_strip_region_view.h`, `.cc`
`chrome-browser-ui-views-frame-horizontal_tab_strip_region_view.{h,cc}.patch`.
- No tab search button and no button leaving the focused tab group: the workspace's pill
  takes their place before the tabs (`oblada_workspace_pill_`,
  `oblada::workspaces::WorkspacePill`), laid out as they were, and listed among the children
  in z-order (`GetChildrenInZOrder()`), which are the ones painted and given events.
- No room kept at the end of the tabs to grab the window by in horizon mode, where the
  toolbar's row above them is there for it.
- Oblada code: `oblada/browser/workspaces/workspace_pill.h`, `oblada/browser/ui/horizontal_tabs.h`.

#### `chrome/browser/ui/views/tabs/tab.h`, `.cc`
`chrome-browser-ui-views-tabs-tab.{h,cc}.patch`. The horizontal tab.
- Widths: in horizon modes the tabs share their row evenly (`SharesRow()`), the active one,
  under the address bar in horizon compact mode, no narrower than
  `CompactActiveTabMinWidth()` says.
- In horizon compact mode the active tab shows nothing of its own but its close button and
  its container's mark: the address bar is over it (`IsObladaUnderLocationBar()`, declared
  in `tab.h`).
- The close button shows on the active tab and on the one under the pointer only, as in the
  sidebar.
- In horizon modes the icon and title are in the middle of the tab.
- The container's color, a bar at the bottom of the tab
  (`oblada::containers::PaintTabIndicator()`).
- Oblada code: `oblada/browser/ui/horizontal_tabs.h`, `oblada/browser/containers/containers.h`.

#### `chrome/browser/ui/views/tabs/common/horizontal_tab_style_views.cc`
`chrome-browser-ui-views-tabs-common-horizontal_tab_style_views.cc.patch`. The tabs are
veils (`tab_style.cc`'s colors): the active one has the same rounded shape as the others,
rather than Chrome's, which flows into the toolbar, and there are neither outlines nor lines
between them.

#### `chrome/browser/ui/views/tabs/new_tab_button.h`, `.cc`
`chrome-browser-ui-views-tabs-new_tab_button.{h,cc}.patch`. The horizontal tabs' new tab
button shows the containers to open the tab in when held down, as the sidebar's does
(`tabs/shared/new_tab_button.cc`, see Containers).
- Oblada code: `oblada/browser/containers/containers_menu_model.h`.

#### `chrome/browser/ui/views/tabs/tab_container_impl.cc`, `tab_group_header.cc`
`chrome-browser-ui-views-tabs-tab_container_impl.cc.patch`,
`chrome-browser-ui-views-tabs-tab_group_header.cc.patch`. Tab groups are workspaces, which
the pill shows: their headers are hidden, and take no room.
- `tab_container_impl.cc` also leaves out Chromium's tab closing mode in horizon modes, whose
  tabs fill their row again at once when one is closed.

### Tabs

#### `chrome/browser/ui/tabs/tab_style.cc`
`chrome-browser-ui-tabs-tab_style.cc.patch`. Tabs are translucent veils of the text color,
as in Zen, instead of Chromium's solid backgrounds. The active, selected and hovered tabs
each get their own strength, for dark and light themes.

#### `chrome/browser/ui/views/tabs/hovercard/tab_hover_card_controller.cc`
`chrome-browser-ui-views-tabs-hovercard-tab_hover_card_controller.cc.patch`. No hover cards:
the sidebar's tabs show their titles already. Horizontal tabs keep them, their titles often
cut short, but while the address bar floats over the page.
- Oblada code: `oblada/browser/ui/horizontal_tabs.h`.

#### `chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h`, `.cc`
`chrome-browser-ui-views-tabs-common-tab_strip_collection_controller.{h,cc}.patch`.
- Adds `CloseTabOrSelection()`: a tab's close button closes all the tabs selected with it,
  as the menu's "Close" does.
- Closing the last tabs of the workspace shown keeps a New Tab page in their place
  (`oblada::workspaces::CloseLastTabs()`).
- Workspaces keep the window's theme.
- Needed by: `tab_view.cc`.
- Oblada code: `oblada/browser/workspaces/workspaces.h`.

### Workspaces

Workspaces are tab groups shown with Chromium's group "focus" mode, one at a time
(`chromium/oblada/browser/workspaces/`).

#### `chrome/browser/ui/ui_features.cc`
`chrome-browser-ui-ui_features.cc.patch`.
- Turns `kTabGroupsFocusing` on, which workspaces are built on. Tabs of hidden workspaces
  keep running instead of being frozen (`kTabGroupsFocusFreezing` off), as in Zen.
- Turns `kSessionRestoreInfobar` off: no infobar offering to restore tabs at startup, as
  Oblada restores them by default (see `session_startup_pref.cc`).

#### `chrome/browser/ui/browser_commands.cc`
`chrome-browser-ui-browser_commands.cc.patch`.
- Closing the last tabs of the workspace shown keeps a New Tab page there, or closes the
  window if they already were one (`CloseLastTabs()`).
- "Next/previous tab group" switches workspace.
- "Copy link" (`CopyURL()`) copies the page's link without its tracking parameters.
- Oblada code: `oblada/browser/workspaces/workspaces.h`,
  `oblada/browser/clean_links/clean_links.h`.

#### `chrome/browser/ui/views/frame/browser_widget.cc`
`chrome-browser-ui-views-frame-browser_widget.cc.patch`. A gray workspace color gives a
hueless (grayscale) theme instead of a bluish one.
- Works with: `oblada/browser/workspaces/workspace_switcher.cc`, which sets the window's
  color.

#### `chrome/browser/ui/views/tabs/common/tab_group_header_view.h`, `.cc`
`chrome-browser-ui-views-tabs-common-tab_group_header_view.{h,cc}.patch`. A group's header
in the sidebar is its workspace's header.
- It shows the workspace's icon and name as a heading: smaller and fainter than a tab's
  title, with a line under it. Only while the sidebar shows headers: the user turned them
  on (Settings > Oblada) and the sidebar looks expanded.
- A "…" button appears on hover.
- Its menu is the workspace's (`ShowObladaWorkspaceMenu()`), instead of Chromium's group
  editor.
- Needs: `vertical_tab_strip_state_controller.h`.
- Oblada code: `oblada/browser/workspaces/workspace_switcher.h`, `workspaces.h`.

#### `chrome/browser/ui/views/tabs/common/tab_group_view_layout.cc`
`chrome-browser-ui-views-tabs-common-tab_group_view_layout.cc.patch`. In the sidebar, a
group whose header is hidden leaves no room for it above its tabs: the workspace's header
shows only if the user wants it.
- Works with: `tab_group_header_view.cc`.

#### `chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h`, `.cc`
`chrome-browser-ui-views-tabs-common-tab_collection_animating_layout_manager.{h,cc}.patch`.
Adds `SnapToTarget()`, which ends an animation with the views where it was taking them.
- Needed by: Oblada's `workspace_slider.cc`, when a workspace's preview hands over to its
  tabs.

#### `chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_focus_swipe_controller.cc`
`chrome-browser-ui-views-tabs-vertical-vertical_tab_strip_focus_swipe_controller.cc.patch`.
A horizontal swipe on the sidebar switches workspace (`oblada::workspaces::SwitchRelative()`).
Currently inactive: the `vertical_tab_strip_region_view.cc` patch no longer creates this
controller, since `WorkspaceSlider` handles the swipes. It can go.

#### `chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_model_listener.cc`, `tab_group_sync_delegate_desktop.cc`
`chrome-browser-ui-tabs-saved_tab_groups-saved_tab_group_model_listener.cc.patch`,
`chrome-browser-ui-tabs-saved_tab_groups-tab_group_sync_delegate_desktop.cc.patch`.
Workspaces are never saved or synced as tab groups. A synced copy could otherwise close,
open or rename their tabs. Sessions still keep them. Both leave Chromium's code in place
behind an early return.

#### `chrome/browser/sessions/session_restore.cc`
`chrome-browser-sessions-session_restore.cc.patch`. Restoring a session neither saves its
workspaces as tab groups nor connects them to saved copies; Chromium's code stays behind an
early return. Chromium saved every restored group not saved yet, with the tabs restored so
far: when a tab was opened in the workspace before the tab group service had loaded (a link
opening Oblada), the patches above left the group unconnected, but its saved copy kept the
workspace's id, so the new tab's next navigation failed `CHECK(tab)` in
`saved_tab_group_web_contents_listener.cc` and crashed the browser. Goes with the
`saved_tab_group_model_listener.cc` and `tab_group_sync_delegate_desktop.cc` patches.

#### `components/saved_tab_groups/public/pref_names.cc`, `components/bookmarks/browser/bookmark_utils.cc`
`components-saved_tab_groups-public-pref_names.cc.patch`,
`components-bookmarks-browser-bookmark_utils.cc.patch`. New groups aren't pinned to the
saved groups bar, nor shown in the bookmarks bar. Only the defaults change.

### Containers

A container is a separate storage partition of the profile
(`chromium/oblada/browser/containers/`).

#### `chrome/browser/chrome_content_browser_client_navigation_throttles.cc`
`chrome-browser-chrome_content_browser_client_navigation_throttles.cc.patch`. Adds the
throttle that reopens a site in the container it's always opened in.
- Oblada code: `oblada/browser/containers/containers.h`.

#### `chrome/browser/ui/tab_helpers.cc`
`chrome-browser-ui-tab_helpers.cc.patch`. Attaches Oblada's tab helpers, for containers and
Firefox add-ons, to every tab.
- Oblada code: `oblada/browser/containers/containers.h`,
  `oblada/browser/firefox_addons/firefox_addons.h`.

#### `chrome/browser/ui/navigator/browser_navigator.cc`
`chrome-browser-ui-navigator-browser_navigator.cc.patch`. A link opened in a new tab from a
container tab stays in the container (`SiteInstanceForNewTab()`).
- Oblada code: `oblada/browser/containers/containers.h`.

#### `content/browser/web_contents/web_contents_impl.cc`
`content-browser-web_contents-web_contents_impl.cc.patch`. A popup opened without an opener
from a container tab stays in its container's storage partition.

#### `chrome/browser/sessions/session_service_base.cc`, `chrome/browser/ui/browser_live_tab_context.cc`, `chrome/browser/ui/browser_tabrestore.cc`
`chrome-browser-sessions-session_service_base.cc.patch`,
`chrome-browser-ui-browser_live_tab_context.cc.patch`,
`chrome-browser-ui-browser_tabrestore.cc.patch`. A tab's container survives restarts and
"Reopen closed tab".
- The first two save it in the tab's extra data (`oblada.container`).
- `browser_tabrestore.cc` creates the restored tab in it (`SiteInstanceForRestoredTab()`).
- `session_service_base.cc` keeps the sidebar's width too (see its entry under the sidebar).
- Oblada code: `oblada/browser/containers/containers.h`.

#### `chrome/browser/resource_coordinator/tab_lifecycle_unit.cc`
`chrome-browser-resource_coordinator-tab_lifecycle_unit.cc.patch`. A discarded tab (to free
memory, by Memory Saver or an extension) stays in its container: Chromium replaces its
WebContents with empty ones, which are now created in the container
(`SiteInstanceForDiscardedTab()`). Without it the tab reloaded outside the container, and a
temporary container whose last tab was discarded was removed with its data.
- Oblada code: `oblada/browser/containers/containers.h`.

#### `chrome/browser/renderer_context_menu/render_view_context_menu.h`, `.cc`
`chrome-browser-renderer_context_menu-render_view_context_menu.{h,cc}.patch`.
- "Open link in new container tab ▸" in a link's context menu, in normal windows.
- "Copy Clean Link" after "Copy link address", for web links, enabled when the link has
  tracking parameters; counted in Chromium's metrics as the link copied. "Copy Clean Link
  to Highlight" (`link_to_text_menu_observer.cc`) is counted as "Copy link to highlight".
- "Block an element", first among the developer items, on sites where the Barrier is up:
  its element picker.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/clean_links/clean_links.h`, `oblada/browser/barrier/cosmetic_filters.h`,
  `oblada/browser/l10n.h`.

#### `chrome/browser/ui/views/tabs/shared/new_tab_button.h`, `.cc`
`chrome-browser-ui-views-tabs-shared-new_tab_button.{h,cc}.patch`. A long press on the New
Tab button opens a menu of containers to open the tab in, as the back button's history menu
does. The button has no background but its hover highlight, like a tab.
- Oblada code: `oblada/browser/containers/containers_menu_model.h`.

#### `chrome/browser/ui/views/tabs/shared/new_tab_button_menu_model.h`, `.cc`
`chrome-browser-ui-views-tabs-shared-new_tab_button_menu_model.{h,cc}.patch`. "New container
tab ▸" in the New Tab button's context menu.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/l10n.h`.

#### `chrome/browser/ui/views/tabs/common/tab_view.cc`
`chrome-browser-ui-views-tabs-common-tab_view.cc.patch`. Draws the tab's container color
(`PaintTabIndicator()`), and its close button closes the tabs selected with it.
- Needs: `tab_strip_collection_controller.h` (`CloseTabOrSelection()`).
- Oblada code: `oblada/browser/containers/containers.h`.

### Address bar

#### `chrome/browser/ui/views/location_bar/location_bar_view.h`, `.cc`
`chrome-browser-ui-views-location_bar-location_bar_view.{h,cc}.patch`. The address bar in
the sidebar, and over the active tab in horizon compact mode, which is the same kind
(`oblada::sidebar::HasSidebarLocationBar()`).
- **Look:** a translucent rounded rectangle without a focus ring.
- **Additions:** the container chip, the site panel's button and the Barrier's
  (`oblada::barrier::AddBarrierButton()`, out of the sidebar, whose site panel has the
  Barrier's controls instead), quiet like the site panel's.
- **The site panel** has the bookmark star and the app's install instead
  (`HidePageActionsInPanel()`), and the site's information: no location icon for a site
  without warnings while the bar isn't edited (`IsObladaSiteInfoInPanel()`).
- **Quiet** (`IsObladaQuiet()`): only the site, until it is hovered, focused or showing one
  of its bubbles. What it hides is clipped to nothing, layers included (a button's icon
  goes on a layer of its own while its hover highlight fades, and would stay above the
  bar).
- **Floating** (`UpdateObladaFloating()`): it floats over the page, Arc-like, while it has
  the focus, with the whole URL however it got the focus (Chromium leaves a click with the
  site alone, which is what the sidebar shows).
- Oblada code: `oblada/browser/ui/floating_location_bar.h`, `sidebar_layout.h`,
  `site_panel.h`, `oblada/browser/containers/container_chip.h`,
  `oblada/browser/barrier/barrier_ui.h`.

#### `chrome/browser/ui/views/frame/top_container_view.cc`
`chrome-browser-ui-views-frame-top_container_view.cc.patch`. Clicks on the floating address
bar aren't taken for drags of the window.
- Oblada code: `oblada/browser/ui/floating_location_bar.h`.

#### `chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc`
`chrome-browser-ui-views-frame-layout-browser_view_layout_impl.cc.patch`. On macOS, no mask
on the top container's layer: it would cut off the floating address bar, which reaches out
of it.

#### `chrome/browser/ui/views/omnibox/omnibox_view_views.cc`
`chrome-browser-ui-views-omnibox-omnibox_view_views.cc.patch`. Escape closes the floating
address bar at once, as Arc's command bar does.
- Oblada code: `oblada/browser/ui/floating_location_bar.h`.

#### `chrome/browser/ui/views/omnibox/omnibox_popup_presenter_base.cc`, `omnibox_popup_webui_base_content.cc`, `rounded_omnibox_results_frame.cc`
`chrome-browser-ui-views-omnibox-omnibox_popup_presenter_base.cc.patch`,
`chrome-browser-ui-views-omnibox-omnibox_popup_webui_base_content.cc.patch`,
`chrome-browser-ui-views-omnibox-rounded_omnibox_results_frame.cc.patch`. The suggestions:
- They wait for the floating bar to land.
- From the sidebar's narrow bar they open wider (`SuggestionsWidth()`).
- Their frame fits the taller floating bar.
These three go together.
- Oblada code: `oblada/browser/ui/floating_location_bar.h`, `sidebar_layout.h`.

#### `components/omnibox/browser/location_bar_model_delegate.cc`
`components-omnibox-browser-location_bar_model_delegate.cc.patch`. The address bar shows
only the site, as Zen and Arc do; the whole URL comes back to be edited.

#### `components/omnibox/browser/aim_eligibility_service_features.cc`
`components-omnibox-browser-aim_eligibility_service_features.cc.patch`. Turns off Google's
"AI Mode" in the address bar.

### Split view

#### `chrome/browser/ui/views/frame/multi_contents_view.h`, `.cc`
`chrome-browser-ui-views-frame-multi_contents_view.{h,cc}.patch`. The panes of a split:
- They float on the window's background like the page, with corners to match.
- Each has a grip above it (`oblada::split::SplitHandle`), to move the pane or take it out.
- Works with: `browser_view_tabbed_layout_impl.cc` (the grips' band).
- Oblada code: `oblada/browser/ui/split_handle.h`.

#### `chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.h`, `.cc`
`chrome-browser-ui-views-frame-multi_contents_view_drop_target_controller.{h,cc}.patch`. A
tab dragged onto the page splits it, Zen-like.
- The page shown is the one active before the drag.
- The tab goes in the half under the pointer (or the bottom band).
- The drop target shows after a short fixed delay.
- Needs: `tab_drag_target.h`, `tab_drag_controller.{h,cc}` (`GetInitiallyActiveTab()`).

#### `chrome/browser/ui/views/tabs/dragging/tab_drag_target.h`, `tab_drag_controller.h`, `.cc`
`chrome-browser-ui-views-tabs-dragging-tab_drag_target.h.patch`,
`chrome-browser-ui-views-tabs-dragging-tab_drag_controller.{h,cc}.patch`.
- **Before the drag:** remembers the tab active then (`GetInitiallyActiveTab()`, a new
  virtual whose default is in `tab_drag_controller.cc`).
- **Over the page:** a tab dragged out of the sidebar stays attached to the window instead
  of tearing off, so it can be dropped to split, and a ghost of it (`oblada::DragGhost`)
  follows the pointer.
- **Headless:** works for the tests.
- Oblada code: `oblada/browser/ui/drag_ghost.h`.

#### `chrome/browser/ui/views/frame/multi_contents_drop_target_view.cc`, `multi_contents_resize_area.h`
`chrome-browser-ui-views-frame-multi_contents_drop_target_view.cc.patch`,
`chrome-browser-ui-views-frame-multi_contents_resize_area.h.patch`.
- The drop target takes the half of the page the tab will take, with the page's corners.
- Its animation is faster.
- The gap between panes matches the page's margins.

#### `chrome/browser/ui/views/frame/contents_container_view.cc`, `contents_container_outline.h`, `.cc`
`chrome-browser-ui-views-frame-contents_container_view.cc.patch`,
`chrome-browser-ui-views-frame-contents_container_outline.{h,cc}.patch`.
- Panes have no mini toolbar of their own.
- The outline goes only around the active pane, with no notch where the toolbar was.
- The outline's corners match the page's.

#### `chrome/browser/ui/views/tabs/common/split_tab_view.h`, `.cc`, `tab_view_vertical_layout.cc`
`chrome-browser-ui-views-tabs-common-split_tab_view.{h,cc}.patch`,
`chrome-browser-ui-views-tabs-common-tab_view_vertical_layout.cc.patch`.
- **In the sidebar:** a split's two tabs share a row on a card, as in Zen.
- **In compact mode:** they are stacked.
- Each shows its close button only when hovered or focused.

### Peek

Option-click (Alt-click) or a force click on a link opens it in a card over the page
(`chromium/oblada/browser/peek/`).

#### `content/public/browser/web_contents_delegate.h`, `.cc`
`content-public-browser-web_contents_delegate.{h,cc}.patch`. Adds the virtual
`HandleForceClick()`, by default false, for the delegate to handle a force click instead of
the system's dictionary lookup.
- Needed by: `render_widget_host_view_mac.mm`, `browser_web_contents_delegate.h`.

#### `content/browser/renderer_host/render_widget_host_view_mac.mm`
`content-browser-renderer_host-render_widget_host_view_mac.mm.patch`. macOS only. A force
click first asks the page's delegate (`HandleForceClick()`), and skips the dictionary
lookup if it handled it.
- Needs: `content/public/browser/web_contents_delegate.h`.

#### `chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h`, `.cc`
`chrome-browser-ui-browser_web_contents_delegate-browser_web_contents_delegate.{h,cc}.patch`.
- `HandleForceClick()` peeks at the link under the mouse.
- `UpdateTargetURL()` records that link.
- `CanDragEnter()` refuses drops on a page under a peek.
- `ShouldFocusLocationBarByDefault()` waits for the transparent New Tab page to load, which
  then focuses the address bar itself.
- `IsPrerender2Supported()` is false for containers' tabs: Chromium prerenders pages in the
  profile's own storage, not the container's.
- Needs: `content/public/browser/web_contents_delegate.h`.
- Oblada code: `oblada/browser/peek/peek.h`, `oblada/browser/new_tab/new_tab_ui.h`,
  `oblada/browser/containers/containers.h`.

#### `chrome/browser/preloading/prefetch/search_prefetch/search_prefetch_service.cc`
`chrome-browser-preloading-prefetch-search_prefetch-search_prefetch_service.cc.patch`. No
search prefetched from containers' tabs, which Chromium does in the profile's own storage;
`chrome_content_browser_client.cc` doesn't serve them one either.
- Oblada code: `oblada/browser/containers/containers.h`.

#### `chrome/browser/ui/tab_helpers.h`
`chrome-browser-ui-tab_helpers.h.patch`. Makes `oblada::peek::PeekTabHelpers` a friend, as
Chromium's Link Preview is: a peek's page gets a tab's helpers as it's created, so that it
loads as a tab's would (extensions see its requests and scripts as a tab's), which it may
become. `extension_tab_util.cc` and `tabs_api.cc` treat it as a tab of no window.
- Oblada code: `oblada/browser/peek/peek.h`.

### Clean links

Links copied without their tracking parameters, as Firefox's "Copy Clean Link", from
Firefox's lists (`chromium/oblada/browser/clean_links/`). The link context menu's item is in
`render_view_context_menu.cc` and "Copy link"'s in `browser_commands.cc` (their entries
above).

#### `chrome/browser/ui/views/omnibox/omnibox_context_menu_mixin.cc`
`chrome-browser-ui-views-omnibox-omnibox_context_menu_mixin.cc.patch`. "Copy Clean Link"
after "Copy" in the address bar's context menu, for the page's link while the bar shows it
(not while the user types), enabled when it has tracking parameters.
- Oblada code: `oblada/browser/clean_links/clean_links.h`, `oblada/browser/l10n.h`.

#### `chrome/browser/renderer_context_menu/link_to_text_menu_observer.cc`
`chrome-browser-renderer_context_menu-link_to_text_menu_observer.cc.patch`. "Copy Clean
Link to Highlight" after "Copy link to highlight", for selected text on a web page: the
same link, made as Chromium makes it, without the page's tracking parameters (its text
selector, in the fragment, kept). Enabled once the link is made, if the page's address has
tracking parameters, as in Firefox.
- Oblada code: `oblada/browser/clean_links/clean_links.h`, `oblada/browser/l10n.h`.

### Safe Browsing

Safe Browsing's lists come from Google's Safe Browsing API (v4) with a key of Oblada's own,
which only releases have: `build.sh --release` takes it from the login keychain (`make
safe-browsing-key`) into the build's `oblada_safe_browsing_api_key`
(`oblada/components/safe_browsing/BUILD.gn`). Chromium's `google_api_key` would give the key
to every Google service it has (Translate, Optimization Guide, password affiliations…),
which would start calling Google with it; Oblada gives it to Safe Browsing alone.

#### `components/safe_browsing/core/browser/db/sb_protocol_config.cc`, `BUILD.gn`
`components-safe_browsing-core-browser-db-sb_protocol_config.cc.patch`,
`components-safe_browsing-core-browser-db-BUILD.gn.patch`. `GetSBProtocolConfig()` gives
Safe Browsing's requests for its lists and full hashes Oblada's key when the build has one,
`google_apis::GetAPIKey()` otherwise. Reports to Google (`GetReportUrl()`, download pings,
password protection) keep `google_apis`' key, which builds don't have: they stay off. The
`BUILD.gn` patch gives `sb_protocol_config` the build flag's header
(`//oblada/components/safe_browsing:buildflags`).

### Barrier

The Barrier ("Barriera"), Brave's protections of privacy in Oblada
(`chromium/oblada/browser/barrier/barrier.h`): blocking trackers and ads with Brave's engine
(adblock-rust), cosmetic filters and scriptlets in pages, fingerprinting protection in Blink,
upgrading to HTTPS, third parties' storage forgotten, tracking parameters and redirects
skipped, Global Privacy Control, web APIs turned off; its switch ("DANGEROUS") turns it all
off. Its button in the address bar is in `location_bar_view.cc`, its controls in the site
panel are Oblada's own, Settings' in `settings_ui.cc`, `route.ts`, `router.ts`,
`settings_main.html` and `prefs_util.cc` (their entries under Internal pages). Its own code is
in four places: `oblada/browser/barrier` (the browser), `oblada/renderer/barrier` (pages'
frames), `oblada/common/barrier` (the interfaces between them), `oblada/blink` (Blink's side
of fingerprinting protection), and `oblada/components/barrier/adblock` (the engine's
bridge).

#### `third_party/rust/chromium_crates_io/Cargo.toml`, `Cargo.lock`, `gnrt_config.toml`, and the `BUILD.gn`/`README.chromium` of `regex`, `aho_corasick`, `bitflags`, `serde`, `serde_core`, `siphasher`, `rand_core`, `stable_deref_trait`
`third_party-rust-….patch`. Brave's ad-blocking engine, the `adblock` crate (0.13.3, MPL-2.0,
with `css-validation` for procedural filters), among Chromium's crates, with what it needs
that Chromium doesn't ship: `url`, `idna`, `flatbuffers`, `seahash`, `thiserror` 1,
`cssparser`, `selectors` and their own. `regex` and `siphasher` were for tests only
(`group = 'test'`); `aho-corasick` and `rand_core` may use `unsafe`, as their code does on
arm64; `flatbuffers`' and `thiserror` 1's build scripts only probe for nightly compilers
(`remove_build_rs`); `selectors`' writes a set of attribute names (`build_script_outputs`).
The other `BUILD.gn` changes are gnrt's, from the crates' new features and groups.
- The crates' sources aren't patches: `chromium/scripts/crates.sh` downloads them with
  gnrt (`run_gnrt.py vendor` and `gen`), as Chromium imports crates, when the lock file
  changes. The licenses three crates don't publish are gnrt's own patches, in
  `chromium/crates/patches`.
- Oblada code: `oblada/components/barrier/adblock` (`rust_static_library` with cxx).
- To update the engine: change its version in `Cargo.toml`, run `crates.sh`, and fix what
  gnrt asks for in `gnrt_config.toml`; then `filter_lists.cc`'s `kEngineVersion`.

#### `chrome/browser/chrome_content_browser_client.cc`
`chrome-browser-chrome_content_browser_client.cc.patch`. `WillCreateURLLoaderFactory()`
puts the Barrier between pages and the network first, before extensions' WebRequest
(`oblada::barrier::MaybeProxyURLLoaderFactory()`): requests blocked, answered with stand-ins,
stripped of tracking parameters, past tracking redirects, with `$csp` rules' policies.
Its requests are also checked under their hosts' canonical names (CNAME uncloaking),
through the resolver of the process's storage partition (the container), which is why it
gets `render_process_id`. Pages' own navigations may get the Barrier's question about Google
sign-in on another site (`oblada/browser/barrier/google_sign_in.h`).
Searches prefetched in the profile's own storage aren't served to containers' tabs
(`oblada::containers::IsContainerFrame()`, in `WillCreateURLLoaderRequestInterceptors()`
and `CreateURLLoaderHandlerForServiceWorkerInitiatedNavigationRequest()`).
`WillInterceptWebSocket()` and `CreateWebSocket()` check frames' WebSocket connections
(`oblada::barrier::WillFilterWebSocket()`, `FilterWebSocket()`), which then go on to
extensions' WebRequest, as Chromium has them, or to the network.
With `chrome/browser/chrome_content_browser_client.h`
(`chrome-browser-chrome_content_browser_client.h.patch`), it also overrides
`GetObladaWorkerFarbling()`: shared and service workers' fingerprinting protection, from
their storage key's top-level site and their container
(`oblada::barrier::GetWorkerFarbling()`).
- Needs: `content_browser_client.h` (`GetObladaWorkerFarbling()`).
- Oblada code: `oblada/browser/barrier/request_filter.h`, `oblada/browser/barrier/farbling.h`.

#### `chrome/browser/chrome_content_browser_client_navigation_throttles.cc`
(Its entry under Containers.) Also holds documents' responses until their cosmetic filters
are there (`MaybeAddCosmeticFiltersThrottle()`), and reads the start of pages' HTML for AMP
pages, which it leaves for the original (`MaybeAddDeAmpThrottle()`).
- Oblada code: `oblada/browser/barrier/cosmetic_filters.h`, `oblada/browser/barrier/de_amp.h`.

#### `chrome/browser/chrome_browser_interface_binders.cc`
`chrome-browser-chrome_browser_interface_binders.cc.patch`. Binds frames'
`oblada.mojom.BarrierHost` (the generic rules their classes and ids call for, the element
picker's filters).
- Oblada code: `oblada/browser/barrier/cosmetic_filters.h`, `oblada/common/barrier`.

#### `chrome/browser/ui/tab_helpers.cc`
(Its entry under Containers.) Also attaches the Barrier's tab helper: what the tab
blocked, the settings sent to its documents, the sites it leaves.
- Oblada code: `oblada/browser/barrier/tab_helper.h`.

#### `chrome/renderer/chrome_content_renderer_client.cc`, `chrome/renderer/BUILD.gn`
`chrome-renderer-chrome_content_renderer_client.cc.patch`, `chrome-renderer-BUILD.gn.patch`.
Each frame gets the Barrier's agent (`RenderFrameCreated()`), which runs the scriptlets and
cosmetic filters once scripts may run in a new document (`RunScriptsAtDocumentStart()`,
before extensions' document_start scripts, as Brave's).
- Oblada code: `oblada/renderer/barrier/barrier_agent.h`.

#### `chrome/common/chrome_isolated_world_ids.h`
`chrome-common-chrome_isolated_world_ids.h.patch`. The Barrier's isolated world
(`ISOLATED_WORLD_ID_OBLADA_BARRIER`), where its cosmetic filters and element picker run,
before extensions' worlds.

#### `chrome/browser/metrics/chrome_feature_list_creator.cc`
`chrome-browser-metrics-chrome_feature_list_creator.cc.patch`.
- Certificate Transparency, as Chrome has it (`kCertificateTransparencyAskBeforeEnabling`,
  which Chromium leaves to Chrome-branded builds), in any case.
- The Barrier's changes to Chromium's features, when it's on at startup, each unless its
  own switch in Settings is off (`oblada::barrier::AddFeatureOverrides()`): web APIs Brave
  turns off (Web Bluetooth, local File System Access, Web Serial, FedCM, WebOTP, Digital
  Credentials, getInstalledRelatedApps, Fenced Frames, FLEDGE, Topics, Attribution
  Reporting, the Reporting API and NEL), connections partitioned by site, Oblada's own
  network features (referrers, cookies' lifetime, HSTS partitioned by site), and Global
  Privacy Control; when it's off, Oblada's network features off. The command line still
  overrides them.
- Oblada code: `oblada/browser/barrier/barrier.h`.

#### `chrome/browser/renderer_preferences_util.cc`, `chrome/browser/ui/prefs/pref_watcher.cc`, `content/browser/renderer_host/render_view_host_impl.cc`
`chrome-browser-renderer_preferences_util.cc.patch`, `chrome-browser-ui-prefs-pref_watcher.cc.patch`,
`content-browser-renderer_host-render_view_host_impl.cc.patch`. Global Privacy Control
(`Sec-GPC`, `navigator.globalPrivacyControl`) as `oblada.barrier.gpc` says, updated as it
changes. Content overwrote the setting with its DevTools override for frames (it's
Chromium's work in progress, "Universal Opt-Out"): it now adds the override to it.
- Needs: `chrome_feature_list_creator.cc` (`GlobalPrivacyControlTest`, which Blink checks).
- Oblada code: `oblada/browser/barrier/barrier.h`.

#### `chrome/browser/ssl/https_upgrades_interceptor.cc`, `chrome/browser/ssl/https_upgrades_util.cc`
`chrome-browser-ssl-https_upgrades_interceptor.cc.patch`,
`chrome-browser-ssl-https_upgrades_util.cc.patch`. Upgrades to HTTPS as the Barrier says
for the site (`oblada::barrier::HttpsUpgradeFor()`): not at all when they're off (or the
Barrier down, or Brave lists the site), with HTTPS-First's warning before HTTP when strict.
- Oblada code: `oblada/browser/barrier/barrier.h`.

#### `net/base/features.{h,cc}`, `net/url_request/url_request_job.cc`, `net/cookies/canonical_cookie.cc`, `third_party/blink/renderer/platform/weborigin/security_policy.cc`
`net-base-features.{h,cc}.patch`, `net-url_request-url_request_job.cc.patch`,
`net-cookies-canonical_cookie.cc.patch`,
`third_party-blink-renderer-platform-weborigin-security_policy.cc.patch`. Brave's
changes to the network, as features of Oblada's on by default (and off when the Barrier
was off at startup, or their switch is): HSTS partitioned by site (`kObladaPartitionHsts`,
see `transport_security_state.{h,cc}`), referrers never more than the origin across origins
(`kObladaReferrerCap`: Blink computes them so, and the network service on redirects, or
they would differ and be dropped), and cookies kept 6 months at most
(`kObladaCookieExpiryCap`).
- Needs: `chrome_feature_list_creator.cc`.

#### `net/http/transport_security_state.{h,cc}`, `net/url_request/url_request_http_job.cc`
`net-http-transport_security_state.{h,cc}.patch`,
`net-url_request-url_request_http_job.cc.patch`. HSTS partitioned by site, as Brave has it
(`kObladaPartitionHsts`, with the features above): what a host's responses say about its
HSTS from inside another site's pages is kept under that top-level site, so that HSTS
can't carry an identifier from site to site; what it says under its own site holds
everywhere. `GetSSLUpgradeDecision()` and `AddHSTSHeader()` take the request's
`NetworkAnonymizationKey` (`PartitionFor()`); the dynamic state's keys (`STSKey()`) are
the host's hash under its own site, as without partitions, so the saved state stays and
checks without a partition (sockets' `ShouldSSLErrorsBeFatal()`) see it, and a hash of the
host and the top-level site under others. The preload list applies everywhere.
- Needs: `net/base/features.{h,cc}`.

#### `content/public/browser/content_browser_client.h`, content's `shared_worker_content_settings_proxy_impl.{h,cc}` and `service_worker_content_settings_proxy_impl.{h,cc}`
`content-public-browser-content_browser_client.h.patch`,
`content-browser-worker_host-shared_worker_content_settings_proxy_impl.{h,cc}.patch`,
`content-browser-service_worker-service_worker_content_settings_proxy_impl.{h,cc}.patch`.
Shared and service workers ask the browser for their fingerprinting protection as they start
(`WorkerContentSettingsProxy::GetObladaFarbling()`, a synchronous call, like the proxy's
others): content asks the embedder, `ContentBrowserClient::GetObladaWorkerFarbling()`, with
the worker's storage partition (its container) and storage key; by default, none.
- Needs: `worker_content_settings_proxy.mojom` (under Blink, below).

#### Blink: fingerprinting protection
`third_party-blink-….patch` (46). The settings of each document (`ExecutionContext`'s
`ObladaFarblingSettings`: the seed, the APIs left alone, fewer languages), set as it's
created through `WebLocalFrame::SetObladaFarbling()` (`web_local_frame.h`,
`web_local_frame_impl.{h,cc}`); passed on to dedicated workers
(`global_scope_creation_params.h`, `dedicated_worker.cc`, `worker_global_scope.cc`) and
worklets (`worklet_global_scope.cc`, from `threaded_worklet_messaging_proxy.cc`,
`paint_worklet_global_scope_proxy.cc`, `layout_worklet_global_scope_proxy.cc`); asked for by
shared and service workers (`worker_content_settings_proxy.mojom`'s `GetObladaFarbling()`,
through `web_content_settings_client.h`, implemented by
`shared_worker_content_settings_proxy.{h,cc}` and
`service_worker_content_settings_proxy.{h,cc}`, called by `worker_global_scope.cc`); the
Oblada code, in Blink's core (`core/BUILD.gn`); and a hook for each API:
- canvas readouts: `base_rendering_context_2d.cc` (getImageData), `html_canvas_element.cc`
  (toDataURL), `canvas_async_blob_creator.cc` (toBlob, convertToBlob), with Chromium's own
  leftover noise (`canvas_interventions/noise_helper.h`);
- audio: `audio_buffer.{h,cc}`, `analyser_node.cc`;
- GPUs' names: `webgl_rendering_context_base.cc`, `gpu_adapter.cc`;
- hardware: `navigator_base.cc` (hardwareConcurrency), `navigator_device_memory.cc`,
  `battery_manager.cc`;
- the screen and the window: `screen.cc`, `local_dom_window.cc`, `media_values.cc`;
- fonts: `css_font_selector.cc`, `css_font_selector_base.cc` (document.fonts.check),
  `offscreen_font_selector.cc` (workers), `local_font_face_source.cc` (`local()`);
- `media_devices.cc` (enumerateDevices), `speech_synthesis.cc` (a voice more),
  `usb_device.{h,cc}` (serial numbers), `keyboard.cc`, `navigator_language.cc`;
- a site's connections: `websocket_channel_impl.{h,cc}` (30 WebSockets at most),
  `event_source.{h,cc}` (250 EventSource connections), counted by
  `oblada/blink/connection_pools.h`.
- Oblada code: `oblada/blink/farbling.h`, `oblada/blink/farbling_settings.h`,
  `oblada/blink/connection_pools.h`.

### Performance

Chromium defaults that other Chromium browsers change too, to start faster and work less in
the background.

#### `chrome/browser/performance_manager/policies/background_tab_loading_policy.cc`
`chrome-browser-performance_manager-policies-background_tab_loading_policy.cc.patch`. Tabs
restored at startup load once opened, as in Firefox and Zen: `ShouldLoad()` only lets the
policy load those showing, pinned tabs (the Essentials) and those known to work in the
background (notifications, or a title or icon seen changing while hidden; Chromium's own
`UsesBackgroundCommunication()` also counts sites it knows nothing about, which would load
the first few restored tabs). The others load when activated.

#### `components/performance_manager/user_tuning/prefs.cc`
`components-performance_manager-user_tuning-prefs.cc.patch`. Memory Saver is on by default.
- Needs: `tab_lifecycle_unit.cc`, so that the tabs it discards stay in their container.

#### `components/download/public/common/download_features.cc`
`components-download-public-common-download_features.cc.patch`. Parallel downloading on
everywhere, as on Android.

#### `components/history_clusters/core/features.cc`
`components-history_clusters-core-features.cc.patch`. Journeys off: no clustering of visits as
pages load, and no "By group" in History.

#### `chrome/browser/performance_manager/chrome_browser_main_extra_parts_performance_manager.cc`
`chrome-browser-performance_manager-chrome_browser_main_extra_parts_performance_manager.cc.patch`.
No `PageResourceMonitor`, which measured every page's CPU and memory every two minutes only
for Chrome's UKM metrics.

#### `components/soda/soda_installer.cc`
`components-soda-soda_installer.cc.patch`. Live Caption's speech recognition (SODA, some 220 MB
with a language) is installed at startup only while a feature using it is on, not because it
was used in the last 30 days: registering it again pushed its removal 30 days on at every
start, so after one use it never went away. Sites using on-device speech recognition install it
again when they need it.

#### `chrome/browser/component_updater/wasm_tts_engine_component_installer.h`, `.cc`, `chrome/browser/ui/read_anything/read_anything_service.cc`
`chrome-browser-component_updater-wasm_tts_engine_component_installer.{h,cc}.patch`,
`chrome-browser-ui-read_anything-read_anything_service.cc.patch`. Read aloud's voice engine
(some 25 MB) is registered, so downloaded, only once Reading mode has been opened, rather than
for everyone at startup: `RegisterWasmTtsEngineComponent()` skips it, and removes an engine
installed earlier, until then, and Reading mode registers it when it opens
(`RegisterAndUpdateOnDemand()`). Chromium forgets Reading mode's last opening after 90 days.
The header also has the `AllowCachedCopies()` below.

#### `chrome/browser/component_updater/soda_component_installer.h`, `soda_language_pack_component_installer.h`, `screen_ai_component_installer.h`
`chrome-browser-component_updater-{soda_component_installer,soda_language_pack_component_installer,screen_ai_component_installer}.h.patch`.
`AllowCachedCopies()` false, as Chromium does for its AI models: the component updater keeps a
copy of each component's package in `component_crx_cache` for smaller updates, which doubled
the space these large ones take (Screen AI, for text recognition in PDFs and Reading mode, is
some 130 MB).

### New Tab page and welcome

#### `chrome/browser/search/search.cc`
`chrome-browser-search-search.cc.patch`. The New Tab page is Oblada's transparent one,
`chrome://oblada-new-tab/`, while the pref `oblada.blank_new_tab` is on (the default),
rather than the search engine's. Incognito keeps its own.
- Oblada code: `oblada/browser/new_tab/new_tab_ui.h`.

#### `chrome/browser/ui/views/frame/contents_web_view.h`, `.cc`
`chrome-browser-ui-views-frame-contents_web_view.{h,cc}.patch`. Oblada's see-through pages
(the New Tab page, `oblada://welcome`) show the window through them. The background is
updated as pages change (`PrimaryPageChanged()`), so other pages are opaque again. For a
see-through page the view is made opaque first, then see-through: a new document's view
(e.g. after a reload) takes its color from the last one without its renderer being told,
which only happens as the view's color changes, and painted its default background.
- Oblada code: `oblada/browser/new_tab/new_tab_ui.h`.

#### `chrome/browser/ui/startup/startup_browser_creator_impl.cc`
`chrome-browser-ui-startup-startup_browser_creator_impl.cc.patch`. The first launch after
installing opens `oblada://welcome` instead of the New Tab page. Chromium's own first run
window doesn't open before it (see `chrome_browser_main.cc`).
- Oblada code: `oblada/browser/welcome/welcome_ui.h`.

### Extensions and Firefox add-ons

Firefox add-ons (`.xpi`) are converted as they install
(`chromium/oblada/browser/firefox_addons/`). Firefox's `contextualIdentities` and
`cookieStoreId` map to containers.

#### `chrome/browser/download/chrome_download_manager_delegate.cc`
`chrome-browser-download-chrome_download_manager_delegate.cc.patch`. A downloaded `.xpi` is
installed as an add-on instead of being saved.
- Oblada code: `oblada/browser/firefox_addons/firefox_addons.h`.

#### `chrome/common/extensions/api/tabs.json`
`chrome-common-extensions-api-tabs.json.patch`. Adds Firefox's optional `cookieStoreId` (a
tab's container) to:
- `Tab`;
- `tabs.create()`'s properties;
- `tabs.query()`'s filter (one or several).
- Needed by: `tabs_api.cc`, `extension_tab_util.cc`.

#### `chrome/browser/extensions/api/tabs/tabs_api.h`, `.cc`
`chrome-browser-extensions-api-tabs-tabs_api.{h,cc}.patch`.
- `tabs.query()` filters by `cookieStoreId`.
- `tabs.create()` opens the tab in the container its `cookieStoreId` names
  (`OpenTabInContainer()`).
- `tabs.executeScript()` and `insertCSS()` also run in a peek's page, which has no window
  (`peek::FindPeekByTabId()`).
- Needs: `tabs.json`, `extension_tab_util.cc`.
- Oblada code: `oblada/browser/containers/containers.h`,
  `oblada/browser/firefox_addons/cookie_store_ids.h`, `oblada/browser/peek/peek.h`.

#### `chrome/browser/extensions/extension_tab_util.cc`
`chrome-browser-extensions-extension_tab_util.cc.patch`.
- Tabs given to Firefox add-ons carry their container's `cookieStoreId`.
- `GetTabById()` finds a peek's page as it finds a prerendered one: a tab in no window, at
  no index.
- Needs: `tabs.json`.
- Oblada code: `oblada/browser/firefox_addons/cookie_store_ids.h`,
  `oblada/browser/peek/peek.h`.

#### `chrome/browser/extensions/api/cookies/cookies_api.cc`
`chrome-browser-extensions-api-cookies-cookies_api.cc.patch`.
- `cookies.*` calls with a Firefox `storeId` use that container's cookies.
- `getAllCookieStores()` lists one store per container in use, for Firefox add-ons.
- Oblada code: `oblada/browser/firefox_addons/cookie_store_ids.h`.

#### `chrome/browser/extensions/chrome_extensions_browser_api_provider.cc`, `chrome/common/extensions/chrome_extensions_api_provider.cc`
`chrome-browser-extensions-chrome_extensions_browser_api_provider.cc.patch`,
`chrome-common-extensions-chrome_extensions_api_provider.cc.patch`. Register Oblada's
extension APIs (`contextualIdentities`) next to Chrome's:
- their functions;
- their schemas;
- their API and manifest features.
- Needs: the two extension `BUILD.gn` patches.
- Oblada code: the generated headers of `oblada/browser/extensions/api` and
  `oblada/common/extensions/api`.

#### `extensions/browser/manifest_v2_handler.cc`
`extensions-browser-manifest_v2_handler.cc.patch`. Manifest V2 extensions keep working,
without warnings, since many Firefox add-ons are MV2.

#### `chrome/browser/ui/views/extensions/extensions_toolbar_desktop.cc`
`chrome-browser-ui-views-extensions-extensions_toolbar_desktop.cc.patch`. Extension popups
open from the site panel's button in the address bar (`oblada::site_panel::ExtensionsAnchor()`),
wherever the toolbar keeps its extension buttons out of sight: in the sidebar, in scaffold
modes and with horizontal tabs.
- Oblada code: `oblada/browser/ui/site_panel.h`.

### Menus

#### `chrome/browser/ui/toolbar/app_menu_model.cc`, `chrome/browser/ui/views/app_menu/action_app_menu_manager.cc`
`chrome-browser-ui-toolbar-app_menu_model.cc.patch`,
`chrome-browser-ui-views-app_menu-action_app_menu_manager.cc.patch`.
- The menu starts with "Reload", when the toolbar doesn't show its button (in a sidebar too
  narrow for it: `oblada::sidebar::ShowsReloadButton()`), and "Window layout ▸" (the
  sidebar's four, then the three with horizontal tabs: `oblada::sidebar::ModeMenuModel`).
- "New container tab ▸" follows the new tab and window items.
- Neither menu offers Chromium's switch to horizontal tabs: the layouts that have them do.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/l10n.h`, `oblada/browser/ui/sidebar_layout.h`, `sidebar_mode.h`.

#### `chrome/browser/ui/views/toolbar/app_menu.cc`, `browser_app_menu_button.cc`
`chrome-browser-ui-views-toolbar-app_menu.cc.patch`,
`chrome-browser-ui-views-toolbar-browser_app_menu_button.cc.patch`. The menu opened from the
sidebar unfolds over the page (to the right). Its button is Zen's horizontal "⋯".

#### `chrome/browser/ui/tabs/tab_menu_model.h`, `.cc`
`chrome-browser-ui-tabs-tab_menu_model.{h,cc}.patch`. A tab's context menu:
- "Move to workspace ▸" replaces Chromium's group items while workspaces are on.
- "Reopen in container ▸" follows "Duplicate".
- No switching to horizontal tabs.
- When rebasing: Chromium's group items moved into an `else` branch.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/workspaces/workspace_switcher.h`, `oblada/browser/l10n.h`.

#### `chrome/browser/ui/cocoa/main_menu_builder.mm`, `chrome/browser/ui/views/frame/system_menu_model_builder.cc`
`chrome-browser-ui-cocoa-main_menu_builder.mm.patch`,
`chrome-browser-ui-views-frame-system_menu_model_builder.cc.patch`. No "Switch to
horizontal/vertical tabs" in macOS's View menu and in the window's system menu. Collapsing
the sidebar stays. The system menu (a right click on the sidebar) has no "Send feedback"
either: it opened Google's feedback form. It has "Edit default theme…" and "Reset default
theme" instead (`oblada::workspaces::AddDefaultThemeItems()`).
- Oblada code: `oblada/browser/workspaces/theme_picker.h`.

#### `chrome/browser/ui/views/frame/system_menu_model_delegate.cc`
`chrome-browser-ui-views-frame-system_menu_model_delegate.cc.patch`. Enables and runs the
system menu's default theme items (`IsDefaultThemeCommandEnabled()`,
`ExecuteDefaultThemeCommand()`), which aren't browser commands.
- Works with: `system_menu_model_builder.cc`.
- Oblada code: `oblada/browser/workspaces/theme_picker.h`.

### URLs and search

#### `components/url_formatter/url_fixer.cc`
`components-url_formatter-url_fixer.cc.patch`. A typed `oblada://x` goes to `chrome://x`,
and a bare `oblada://` to `chrome://about`, the list of every page.
- Needed by: `builtin_provider.cc`.

#### `components/url_formatter/url_formatter.cc`
`components-url_formatter-url_formatter.cc.patch`. `chrome://` URLs are shown as
`oblada://`. Both names are six letters, so nothing after the scheme moves.
- Works with: `url_fixer.cc`.

#### `chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.cc`
`chrome-browser-autocomplete-chrome_autocomplete_scheme_classifier.cc.patch`. A typed
`oblada://…` is a URL to open, not a search.

#### `components/omnibox/browser/builtin_provider.cc`
`components-omnibox-browser-builtin_provider.cc.patch`. The address bar suggests the
internal pages as `oblada://`, or as `chrome://` when that's what is typed.
- Needs: `url_fixer.cc` (its suggestions go through `FixupURL()`).

#### `components/search_engines/template_url_prepopulate_data.cc`
`components-search_engines-template_url_prepopulate_data.cc.patch`. DuckDuckGo is the
default search engine in every region, unless the user picks another.

### Internal pages

#### `chrome/browser/ui/webui/chrome_web_ui_configs.cc`
`chrome-browser-ui-webui-chrome_web_ui_configs.cc.patch`. Registers Oblada's pages before
Chrome's:
- `chrome://containers`;
- `chrome://oblada-new-tab`;
- `chrome://welcome`.
Their hosts must not clash with Chrome's.
- Oblada code: `oblada/browser/containers/containers_ui.h` (`oblada::RegisterWebUIConfigs()`).

#### `chrome/browser/ui/webui/settings/settings_ui.cc`
`chrome-browser-ui-webui-settings-settings_ui.cc.patch`.
- Hides the Settings page about Google's AI features.
- The About page has Oblada's version (with Chromium's), its copyright and a link to its
  source code.
- Serves Settings > Oblada, its module and its strings (`AddObladaPage()`), and the
  Barrier's messages (`oblada::settings::BarrierHandler`: its switch, filter lists and
  sites).
- Oblada code: `oblada/browser/about/about_page.h`,
  `oblada/browser/settings/oblada_page.h`, `oblada/browser/settings/barrier_handler.h`.

#### `chrome/browser/ui/webui/settings/about_handler.cc`
`chrome-browser-ui-webui-settings-about_handler.cc.patch`. The About page's update status
is Oblada's updater's (`oblada::updater::CreateVersionUpdater()`) rather than Chromium's,
whose check fails with "error code 0": opening the page checks the feed, shows the
download, then "Relaunch" once the update is ready. A build that doesn't update itself
shows no status.
- Oblada code: `oblada/browser/updater/updater.h`.
- Works with: `application_lifetime_desktop.cc`, which installs the update on Relaunch.

#### `chrome/browser/resources/settings/page_visibility.ts`
`chrome-browser-resources-settings-page_visibility.ts.patch`. Settings has no "You and
Google" page (a Google account, sync), which Oblada has no use for. Its routes go with it,
Settings' import dialog included: importing is Oblada's own (see `chrome_pages.cc`). The
Appearance page has no "Theme" row: a window takes its colors from its workspace's theme.

#### `chrome/browser/resources/settings/appearance_page/appearance_page.html.ts`
`chrome-browser-resources-settings-appearance_page-appearance_page.html.ts.patch`. The
Appearance page has no "Customize your toolbar" row: Oblada's toolbar is the sidebar's row
of buttons.

#### `chrome/browser/resources/settings/settings_main/settings_main.html`, `.ts`, `settings_menu/settings_menu.html`, `BUILD.gn`
`chrome-browser-resources-settings-settings_main-settings_main.{html,ts}.patch`,
`chrome-browser-resources-settings-settings_menu-settings_menu.html.patch` and
`chrome-browser-resources-settings-BUILD.gn.patch`. Settings > Oblada
(`oblada://settings/oblada`), Oblada's own settings, first in the menu with Oblada's logo.
Its element, `settings-oblada-page`, is served by Oblada (`oblada/browser/settings/
oblada_page.h`) as a module rather than built with Settings, as Oblada adds no files to
Chromium's tree: `settings_main.ts` imports `/oblada/oblada_page.js`, which `BUILD.gn` maps
to a declaration for the compiler (`oblada/browser/settings/oblada_page.d.ts`, as Chromium
does for `/strings.m.js`) and leaves out of the bundle (`optimize_webui_excludes`), and
`settings_main.html` shows it, as a view of its own, with the route it's on
(`route-path`), for its subpages.
- Needs: `route.ts`, `router.ts`, `prefs_util.cc` (its preferences), `settings_ui.cc` (the
  module and its strings).

#### `chrome/browser/resources/settings/route.ts`, `router.ts`
`chrome-browser-resources-settings-{route,router}.ts.patch`. The route of Settings > Oblada,
`routes.OBLADA`, and of its subpages, the Barrier's filter lists (`OBLADA_FILTERS`,
`/oblada/filters`) and sites with settings of their own (`OBLADA_SITES`, `/oblada/sites`).

#### `chrome/browser/extensions/api/settings_private/prefs_util.cc`
`chrome-browser-extensions-api-settings_private-prefs_util.cc.patch`. Lets Settings change
Oblada's preferences (`oblada.sidebar.mode`, `oblada.workspaces.show_header`, the Barrier's
`oblada.barrier.*`) and WebRTC's IP handling (`webrtc.ip_handling_policy`, which Chromium
has no setting for), named as strings: the file's target can't include Oblada's code.

#### `chrome/browser/ui/chrome_pages.cc`
`chrome-browser-ui-chrome_pages.cc.patch`.
- Help (the Help menu, the About page's "Get help") opens Oblada's GitHub issues rather
  than Google's help center.
- "Import bookmarks and settings" opens Oblada's import, in its welcome
  (`oblada://welcome/#import`), as Settings' import dialog went with "You and Google"
  (see `page_visibility.ts`).
- Oblada code: `oblada/browser/about/about_page.h`, `oblada/browser/welcome/welcome_ui.h`.

#### `chrome/browser/ui/webui/version/version_ui.cc`
`chrome-browser-ui-webui-version-version_ui.cc.patch`. `oblada://version` shows Oblada's
version (`chromium/oblada/VERSION`) and the Chromium it's built on, and Oblada's authors and
copyright, where Chromium named its own (Chromium is credited in `oblada://credits`). Its
logo is Oblada's from `chromium/overrides/components/resources` (`branding/export.sh`).
- Oblada code: the generated `oblada/version.h` (see `oblada/browser/sources.gni`),
  `oblada/browser/about/about_page.h` (`Authors()`, `Copyright()`).

#### `components/webui/version/resources/about_version.html`
`components-webui-version-resources-about_version.html.patch`. No "(Official Build)" or
"(Developer Build)" after the version, which only says how it was compiled: releases are
official builds (`chromium/args.release.gn`), development builds aren't.

#### `components/webui/chrome_urls/resources/app.ts`, `app.html.ts`
`components-webui-chrome_urls-resources-app.ts.patch`,
`components-webui-chrome_urls-resources-app.html.ts.patch`. `chrome://chrome-urls` is
titled "List of Oblada URLs" and shows every page as `oblada://`. The links still go to
`chrome://`.

## Maintenance notes

- **Inactive patch:** `vertical_tab_strip_focus_swipe_controller.cc` (see above).
- **Lines without `// Oblada` marks.** Some added lines lack them. A cleanup pass should
  mark them:
  - `multi_contents_view_drop_target_controller.cc`: its new includes and
    `ShowInitiallyActiveTab()`;
  - `tab_collection_animating_layout_manager.cc`: `SnapToTarget()`;
  - `tab_drag_controller.cc`: `GetInitiallyActiveTab()`;
  - `tab_strip_collection_controller.cc`: the body of `CloseTabOrSelection()`;
  - `tab_group_header_view.cc`: its new includes;
  - `split_tab_view.cc`: its new includes;
  - `new_tab_button.h`: the `weak_ptr.h` include;
  - `location_bar_view.cc`: a few calls in `OnOmniboxBlurred()` and `OnOmniboxHovered()`.
- **Hard to rebase.** These patches have the most hunks inside Chromium's functions and are
  the first to check after a Chromium update:
  - `browser_view_tabbed_layout_impl.cc`;
  - `location_bar_view.cc`;
  - `tab_menu_model.cc`;
  - `toolbar_view.cc`;
  - `browser_native_widget_mac.mm` (its `GetGlassFrameWidth()` was emptied).
