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

There are 159 patches for Chromium 156 (`chromium/CHROMIUM_VERSION`). This file describes
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

**Oblada code that needs a patch to compile.**
`chromium/oblada/browser/workspaces/workspace_slider.cc` calls
`TabCollectionAnimatingLayoutManager::SnapToTarget()`, which the
`tab_collection_animating_layout_manager` patches add.

**Patches that only work together**, though each compiles alone:

- **Containers in sessions:** `session_service_base.cc` and `browser_live_tab_context.cc` save
  a tab's container (under `oblada.container`); `browser_tabrestore.cc` reads it back.
- **`oblada://`:**
  - `url_fixer.cc` maps a typed `oblada://` to `chrome://`;
  - `url_formatter.cc` shows `chrome://` as `oblada://`;
  - `chrome_autocomplete_scheme_classifier.cc` treats the typed text as a URL;
  - `builtin_provider.cc` suggests the pages;
  - the `chrome_urls` page lists them.
- **Always-vertical tabs:** `tab_strip_prefs.cc` and `vertical_tab_strip_state_controller_impl.cc`
  force them on. These patches remove every way back to horizontal tabs:
  - `browser_command_controller.cc`;
  - `main_menu_builder.mm`;
  - `app_menu_model.cc`;
  - `action_app_menu_manager.cc`;
  - `system_menu_model_builder.cc`;
  - `tab_menu_model.cc`.
- **The sidebar layout:** `browser_view_tabbed_layout_impl.cc` puts the toolbar in the
  sidebar (through `oblada::sidebar::SetToolbarInSidebar()`). That is what `toolbar_view.cc`,
  `vertical_tab_strip_region_view.cc` and `browser_native_widget_mac.mm` react to.
- **The window's layouts** (sidebar, scaffold, compact: `oblada/browser/ui/sidebar_mode.h`):
  `vertical_tab_strip_state_controller_impl.cc` starts each window collapsed or not as the
  layout says, `vertical_tab_strip_region_view.cc` keeps the two in step
  (`oblada::sidebar::ModeKeeper`), `browser_view_tabbed_layout_impl.cc` lays each out,
  `toolbar_view.cc` drops its extra buttons in scaffold mode, and `app_menu_model.cc` and
  Settings > Oblada pick one.
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
| Sidebar & compact mode | `browser_view_tabbed_layout_impl.cc`, `browser_view.cc`, `tabs/features.cc`, `vertical_tab_strip_background_blur_backdrop.cc`, `toolbar_view.cc`, `vertical_tab_strip_region_view.{h,cc}`, `vertical_tab_strip_state.h`, `vertical_tab_strip_state_controller{.h,_impl.cc}`, `tab_strip_prefs.cc`, `browser_command_controller.cc`, `vertical_tab_strip_bottom_container.cc`, `download_toolbar_ui_controller.cc` |
| Tabs | `tab_style.cc`, `tab_hover_card_controller.cc`, `tab_strip_collection_controller.{h,cc}` |
| Workspaces | `ui_features.cc`, `browser_commands.cc`, `browser_widget.cc`, `tab_group_header_view.{h,cc}`, `tab_group_view_layout.cc`, `tab_collection_animating_layout_manager.{h,cc}`, `vertical_tab_strip_focus_swipe_controller.cc`, `saved_tab_group_model_listener.cc`, `tab_group_sync_delegate_desktop.cc`, `saved_tab_groups/public/pref_names.cc`, `bookmark_utils.cc` |
| Containers | `chrome_content_browser_client_navigation_throttles.cc`, `tab_helpers.cc`, `browser_navigator.cc`, `web_contents_impl.cc`, `session_service_base.cc`, `browser_live_tab_context.cc`, `browser_tabrestore.cc`, `tab_lifecycle_unit.cc`, `render_view_context_menu.{h,cc}`, `new_tab_button.{h,cc}`, `new_tab_button_menu_model.{h,cc}`, `tab_view.cc` |
| Address bar | `location_bar_view.{h,cc}`, `top_container_view.cc`, `browser_view_layout_impl.cc`, `omnibox_view_views.cc`, `omnibox_popup_presenter_base.cc`, `omnibox_popup_webui_base_content.cc`, `rounded_omnibox_results_frame.cc`, `location_bar_model_delegate.cc`, `aim_eligibility_service_features.cc` |
| Split view | `multi_contents_view.{h,cc}`, `multi_contents_drop_target_view.cc`, `multi_contents_view_drop_target_controller.{h,cc}`, `multi_contents_resize_area.h`, `contents_container_view.cc`, `contents_container_outline.{h,cc}`, `split_tab_view.{h,cc}`, `tab_view_vertical_layout.cc`, `tab_drag_controller.{h,cc}`, `tab_drag_target.h` |
| Peek | `web_contents_delegate.{h,cc}` (content), `render_widget_host_view_mac.mm`, `browser_web_contents_delegate.{h,cc}`, `tab_helpers.h` |
| Clean links | `omnibox_context_menu_mixin.cc`, `link_to_text_menu_observer.cc`, `render_view_context_menu.cc`, `browser_commands.cc` |
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
`oblada::RegisterLocalStatePrefs()`, for the updater's. New Oblada prefs go in
`oblada/browser/prefs.cc`, not here.
- Oblada code: `oblada/browser/prefs.h`.

#### `chrome/browser/prefs/session_startup_pref.cc`
`chrome-browser-prefs-session_startup_pref.cc.patch`. On startup, Oblada reopens the last
session's tabs (and so its workspaces) by default, as Zen and Arc do; Settings' "On
startup" can still change it. Needed by: `ui_features.cc` turning off the infobar that
offered it.

#### `base/check.cc`
`base-check.cc.patch`. `DUMP_WILL_BE_CHECK` failures log an error and go on instead of
crashing, as they would in a non-official build like Oblada's. Non-fatal check failures no
longer write crash dumps, which would have nowhere to go and would pile up next to the real
ones.

#### `content/browser/webui/web_ui_main_frame_observer.cc`
`content-browser-webui-web_ui_main_frame_observer.cc.patch`. A JavaScript error on an
internal page no longer crashes the browser, as it would in a non-official build.

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
  it (Chromium's opaque view under the glass would cover the whole window then).
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
  - **Compact** (collapsed): only the page. The narrow tab strip is out of the window but
    for the margin at its left edge, where the pointer expands it on hover: a card widens
    over the page from there into a panel (Chromium's expand-on-hover width), what's in it
    laid out at its full width from the start, clipped to the card (`ClipToCard()`): the
    toolbar comes down at its top, fading in (Chromium's expand-on-hover opacity), the tabs
    below it. Hidden, the toolbar is laid out just out of the window, for its address bar
    to float over the page from there. Chromium's blur backdrop is the card (`SetCard()`,
    see `vertical_tab_strip_background_blur_backdrop.cc`), opaque; the strip and the
    toolbar are see-through over it, without their outline, shadow or blur.
- **Page:** inset by `ContentInsets()` and rounded by `ContentCornerRadius()`, floating next
  to the sidebar.
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
at the top of the window in scaffold mode, without the profile and extensions buttons
there too (`oblada::sidebar::IsScaffoldToolbar()`).
- Two rows: the buttons next to the window controls, then the address bar's own row.
- Hidden: avatar, reload (moved to the menu), overflow and extensions buttons.
- Smaller button insets.
- `LayOutLocationBar()` lets the address bar float out of it.
- Always drops the split-tabs and Battery Saver buttons.
- Never creates the media button: media is controlled at the bottom of the sidebar
  (`oblada::media::MediaControls`, added by the `vertical_tab_strip_region_view.cc` patch).
- Needs: the sidebar layout (which sets `ToolbarInset()`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `floating_location_bar.h`.

#### `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h`, `.cc`
`chrome-browser-ui-views-frame-vertical_tab_strip_region_view.{h,cc}.patch`. The tab
strip's region in the sidebar.
- Adds the workspace switcher at the bottom of the content area (`oblada_workspaces_`),
  and the media controls above it (`oblada_media_`); the media controls turn compact with
  the strip, the switcher a column in scaffold mode (`oblada::sidebar::Expansion()`).
- Keeps the strip collapsed as the window's layout says, and the layout as the strip is
  collapsed or expanded (`oblada_mode_`, `oblada::sidebar::ModeKeeper`).
- In scaffold mode, no expanding on hover.
- Drops Chromium's swipe controller: workspace swipes are `oblada::workspaces::WorkspaceSlider`'s.
- In the sidebar and in scaffold mode, leaves the row of buttons above the tabs out of its
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
- **`_impl.cc`:** forces vertical tabs on, whatever the pref, and clamps the width to the
  minimum (fixing narrower widths saved before). A window starts collapsed or not as its
  layout says (`oblada::sidebar::CollapsesTabStrip()`), whatever the session saved: the
  layout is the same in every window.
- Needs: `vertical_tab_strip_state.h`.
- Needed by: `vertical_tab_strip_region_view.cc`, `tab_group_header_view.cc`.

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

### Tabs

#### `chrome/browser/ui/tabs/tab_style.cc`
`chrome-browser-ui-tabs-tab_style.cc.patch`. Tabs are translucent veils of the text color,
as in Zen, instead of Chromium's solid backgrounds. The active, selected and hovered tabs
each get their own strength, for dark and light themes.

#### `chrome/browser/ui/views/tabs/hovercard/tab_hover_card_controller.cc`
`chrome-browser-ui-views-tabs-hovercard-tab_hover_card_controller.cc.patch`. No hover cards:
the sidebar's tabs show their titles already.

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
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/clean_links/clean_links.h`, `oblada/browser/l10n.h`.

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
the sidebar.
- **Look:** a translucent rounded rectangle without a focus ring.
- **Additions:** the container chip and the site panel's button.
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
  `site_panel.h`, `oblada/browser/containers/container_chip.h`.

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
- Needs: `content/public/browser/web_contents_delegate.h`.
- Oblada code: `oblada/browser/peek/peek.h`, `oblada/browser/new_tab/new_tab_ui.h`.

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

### Performance

Chromium defaults that other Chromium browsers change too, to start faster and work less in
the background.

#### `chrome/browser/performance_manager/policies/background_tab_loading_policy.cc`
`chrome-browser-performance_manager-policies-background_tab_loading_policy.cc.patch`. Tabs
restored at startup load once opened, as in Firefox and Zen: `ShouldLoad()` only lets the
policy load those showing, pinned tabs (the Essentials) and those that work in the background
(notifications, or a title or icon changing while hidden). The others load when activated.

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
updated as pages change (`PrimaryPageChanged()`), so other pages are opaque again.
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
open from the extensions entry in the address bar (the toolbar's button is hidden in the
sidebar).
- Oblada code: `oblada/browser/ui/site_panel.h`.

### Menus

#### `chrome/browser/ui/toolbar/app_menu_model.cc`, `chrome/browser/ui/views/app_menu/action_app_menu_manager.cc`
`chrome-browser-ui-toolbar-app_menu_model.cc.patch`,
`chrome-browser-ui-views-app_menu-action_app_menu_manager.cc.patch`.
- The menu starts with "Reload" (no longer next to back and forward) and "Window layout ▸"
  (sidebar, scaffold, compact: `oblada::sidebar::ModeMenuModel`).
- "New container tab ▸" follows the new tab and window items.
- Neither menu offers horizontal tabs.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/l10n.h`, `oblada/browser/ui/sidebar_mode.h`.

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
- Serves Settings > Oblada, its module and its strings (`AddObladaPage()`).
- Oblada code: `oblada/browser/about/about_page.h`,
  `oblada/browser/settings/oblada_page.h`.

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
`settings_main.html` shows it, as a view of its own.
- Needs: `route.ts`, `router.ts`, `prefs_util.cc` (its preferences), `settings_ui.cc` (the
  module and its strings).

#### `chrome/browser/resources/settings/route.ts`, `router.ts`
`chrome-browser-resources-settings-{route,router}.ts.patch`. The route of Settings > Oblada,
`routes.OBLADA`.

#### `chrome/browser/extensions/api/settings_private/prefs_util.cc`
`chrome-browser-extensions-api-settings_private-prefs_util.cc.patch`. Lets Settings change
Oblada's preferences (`oblada.sidebar.mode`, `oblada.workspaces.show_header`), named as
strings: the file's target can't include Oblada's code.

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
`components-webui-version-resources-about_version.html.patch`. No "(Developer Build)" after
the version: Chromium says so of any build Google doesn't make officially, Oblada's
releases included.

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
