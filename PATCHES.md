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

There are 129 patches for Chromium 155 (`chromium/CHROMIUM_VERSION`). This file describes
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
| `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h` | `SetObladaShownExpanded()` |
| `chrome/browser/ui/views/tabs/common/tab_group_header_view.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h` | `IsObladaShownExpanded()`, `RegisterOnObladaShownExpandedChanged()` |
| `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h` | `chrome/browser/ui/tabs/vertical_tab_strip_state.h` | `kVerticalTabStripMinUncollapsedWidth` |
| `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.cc` | `chrome/browser/ui/tabs/vertical_tab_strip_state.h` | `kVerticalTabStripMinUncollapsedWidth` |
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
- **Always-vertical tabs:** `tab_strip_prefs.cc` and `vertical_tab_strip_state_controller.cc`
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
| Sidebar & compact mode | `browser_view_tabbed_layout_impl.cc`, `toolbar_view.cc`, `vertical_tab_strip_region_view.{h,cc}`, `vertical_tab_strip_state.h`, `vertical_tab_strip_state_controller.{h,cc}`, `tab_strip_prefs.cc`, `browser_command_controller.cc`, `vertical_tab_strip_bottom_container.cc`, `download_toolbar_ui_controller.cc` |
| Tabs | `tab_style.cc`, `tab_hover_card_controller.cc`, `tab_strip_collection_controller.{h,cc}` |
| Workspaces | `ui_features.cc`, `browser_commands.cc`, `browser_widget.cc`, `tab_group_header_view.{h,cc}`, `tab_collection_animating_layout_manager.{h,cc}`, `vertical_tab_strip_focus_swipe_controller.cc`, `saved_tab_group_model_listener.cc`, `tab_group_sync_delegate_desktop.cc`, `saved_tab_groups/public/pref_names.cc`, `bookmark_utils.cc` |
| Containers | `chrome_content_browser_client_navigation_throttles.cc`, `tab_helpers.cc`, `browser_navigator.cc`, `web_contents_impl.cc`, `session_service_base.cc`, `browser_live_tab_context.cc`, `browser_tabrestore.cc`, `render_view_context_menu.{h,cc}`, `new_tab_button.{h,cc}`, `new_tab_button_menu_model.{h,cc}`, `tab_view.cc` |
| Address bar | `location_bar_view.{h,cc}`, `top_container_view.cc`, `browser_view_layout_impl.cc`, `omnibox_view_views.cc`, `omnibox_popup_presenter_base.cc`, `omnibox_popup_webui_base_content.cc`, `rounded_omnibox_results_frame.cc`, `location_bar_model_delegate.cc`, `aim_eligibility_service_features.cc` |
| Split view | `multi_contents_view.{h,cc}`, `multi_contents_drop_target_view.cc`, `multi_contents_view_drop_target_controller.{h,cc}`, `multi_contents_resize_area.h`, `contents_container_view.cc`, `contents_container_outline.{h,cc}`, `split_tab_view.{h,cc}`, `tab_view_vertical_layout.cc`, `tab_drag_controller.{h,cc}`, `tab_drag_target.h` |
| Peek | `web_contents_delegate.{h,cc}` (content), `render_widget_host_view_mac.mm`, `browser_web_contents_delegate.{h,cc}` |
| New Tab & welcome | `search.cc`, `contents_web_view.{h,cc}`, `startup_browser_creator_impl.cc` |
| Extensions & Firefox add-ons | `chrome_download_manager_delegate.cc`, `cookies_api.cc`, `tabs_api.{h,cc}`, `tabs.json`, `extension_tab_util.cc`, `chrome_extensions_browser_api_provider.cc`, `chrome_extensions_api_provider.cc`, `manifest_v2_handler.cc`, `extensions_toolbar_desktop.cc` |
| Menus | `app_menu_model.cc`, `action_app_menu_manager.cc`, `app_menu.cc`, `browser_app_menu_button.cc`, `tab_menu_model.{h,cc}`, `main_menu_builder.mm`, `system_menu_model_builder.cc` |
| URLs & search | `url_fixer.cc`, `url_formatter.cc`, `chrome_autocomplete_scheme_classifier.cc`, `builtin_provider.cc`, `template_url_prepopulate_data.cc` |
| Internal pages | `chrome_web_ui_configs.cc`, `settings_ui.cc`, `page_visibility.ts`, `about_handler.cc`, `chrome_pages.cc`, `version_ui.cc`, `chrome_urls/resources/app.{ts,html.ts}` |

## The patches

Each entry gives the patch, what it does, what it **needs** (other patches it doesn't compile
or work without) and the Oblada code it calls.

### Build

#### `chrome/browser/ui/BUILD.gn`
`chrome-browser-ui-BUILD.gn.patch`. Imports `//oblada/browser/sources.gni` and adds
`oblada_ui_sources` and `oblada_ui_deps` to `static_library("ui")`. Oblada's code is then
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
- Makes the glass span the whole window, with the page floating on it.
- Centers the window controls on the sidebar's row of buttons.
- Hides "Always show toolbar in full screen", which tabbed windows don't need.
- Needs: the sidebar layout (`ToolbarInset()`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`.

#### `chrome/browser/ui/views/frame/browser_frame_view_mac.mm`
`chrome-browser-ui-views-frame-browser_frame_view_mac.mm.patch`. macOS only. In full screen,
the window's content starts below the menu bar that slides in at the top, as in Safari,
instead of under it.
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
- **Sidebar mode** (vertical tabs expanded): the toolbar is laid out at the top of the tab
  column.
- **Compact mode** (collapsed): the toolbar slides in at the top of the column as the strip
  expands on hover, and the tabs move down.
- **Page:** inset by `ContentInsets()` and rounded by `ContentCornerRadius()`, floating next
  to the sidebar.
- **Split view:** the panes get a top band for their grips.
- **Colors:** the toolbar gets the sidebar's color, opaque over the page in compact mode.
- **Removed:** Chromium's tab-strip corners, caption exclusion and separators.
- **Window controls:** in compact mode they show only once the strip has expanded.
- Needs: `vertical_tab_strip_state.h` (default width).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `floating_location_bar.h`,
  `split_handle.h`, `traffic_lights.h`.
- When rebasing, expect conflicts here first: there are many hunks across
  `CalculateProposedLayout()`, `ConfigureTopContainerBackground()`,
  `DoPreLayoutComputations()` and `DoPostLayoutVisualAdjustments()`.

#### `chrome/browser/ui/views/toolbar/toolbar_view.cc`
`chrome-browser-ui-views-toolbar-toolbar_view.cc.patch`. The toolbar in the sidebar.
- Two rows: the buttons next to the window controls, then the address bar's own row.
- Hidden: avatar, reload (moved to the menu), overflow and extensions buttons.
- Smaller button insets.
- `LayOutLocationBar()` lets the address bar float out of it.
- Always drops the split-tabs and Battery Saver buttons.
- Needs: the sidebar layout (which sets `ToolbarInset()`).
- Oblada code: `oblada/browser/ui/sidebar_layout.h`, `floating_location_bar.h`.

#### `chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h`, `.cc`
`chrome-browser-ui-views-frame-vertical_tab_strip_region_view.{h,cc}.patch`. The tab
strip's region in the sidebar.
- Adds the workspace switcher at the bottom (`oblada_workspaces_`).
- Drops Chromium's swipe controller: workspace swipes are `oblada::workspaces::WorkspaceSlider`'s.
- In the sidebar, hides the row of buttons above the tabs.
- Tells the state controller whether the strip looks expanded.
- In compact mode, the toolbar counts as part of the strip for expand-on-hover.
- The strip collapses while the address bar floats over the page.
- The address bar's suggestions keep the compact sidebar open.
- Needs: `vertical_tab_strip_state_controller.h`, `vertical_tab_strip_state.h`.
- Oblada code: `oblada/browser/workspaces/workspace_switcher.h`,
  `oblada/browser/ui/sidebar_layout.h`, `floating_location_bar.h`.

#### `chrome/browser/ui/tabs/vertical_tab_strip_state.h`
`chrome-browser-ui-tabs-vertical_tab_strip_state.h.patch`. The default sidebar width goes
from 240 to 280, and `kVerticalTabStripMinUncollapsedWidth` (170) is the narrowest it gets,
so the toolbar always fits.
- Needed by: `vertical_tab_strip_state_controller.cc`, `vertical_tab_strip_region_view.h`,
  `browser_view_tabbed_layout_impl.cc`.

#### `chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h`, `.cc`
`chrome-browser-ui-tabs-vertical_tab_strip_state_controller.{h,cc}.patch`.
- **`.h`:** adds `IsObladaShownExpanded()`, `SetObladaShownExpanded()` and
  `RegisterOnObladaShownExpandedChanged()`, which track whether the strip looks expanded
  (rather than compact).
- **`.cc`:** forces vertical tabs on, whatever the pref, and clamps the width to the minimum
  (fixing narrower widths saved before).
- Needs: `vertical_tab_strip_state.h`.
- Needed by: `vertical_tab_strip_region_view.cc`, `tab_group_header_view.cc`.

#### `chrome/browser/ui/tabs/tab_strip_prefs.cc`
`chrome-browser-ui-tabs-tab_strip_prefs.cc.patch`. Vertical tabs are on by default.

#### `chrome/browser/ui/browser_command_controller.cc`
`chrome-browser-ui-browser_command_controller.cc.patch`. Disables the command that switches
to horizontal tabs.

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
- Oblada code: `oblada/browser/workspaces/workspaces.h`.

#### `chrome/browser/ui/views/frame/browser_widget.cc`
`chrome-browser-ui-views-frame-browser_widget.cc.patch`. A gray workspace color gives a
hueless (grayscale) theme instead of a bluish one.
- Works with: `oblada/browser/workspaces/workspace_switcher.cc`, which sets the window's
  color.

#### `chrome/browser/ui/views/tabs/common/tab_group_header_view.h`, `.cc`
`chrome-browser-ui-views-tabs-common-tab_group_header_view.{h,cc}.patch`. A group's header
in the sidebar is its workspace's header.
- It shows the workspace's icon and name, only while the sidebar looks expanded.
- A "…" button appears on hover.
- Its menu is the workspace's (`ShowObladaWorkspaceMenu()`), instead of Chromium's group
  editor.
- Needs: `vertical_tab_strip_state_controller.h`.
- Oblada code: `oblada/browser/workspaces/workspace_switcher.h`, `workspaces.h`.

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

#### `chrome/browser/renderer_context_menu/render_view_context_menu.h`, `.cc`
`chrome-browser-renderer_context_menu-render_view_context_menu.{h,cc}.patch`. "Open link in
new container tab ▸" in a link's context menu, in normal windows.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
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
the sidebar.
- **Look:** a translucent rounded rectangle without a focus ring.
- **Additions:** the container chip and the site panel's button.
- **The site panel** has the bookmark star and the app's install instead
  (`HidePageActionsInPanel()`).
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
- Needs: `tabs.json`.
- Oblada code: `oblada/browser/containers/containers.h`,
  `oblada/browser/firefox_addons/cookie_store_ids.h`.

#### `chrome/browser/extensions/extension_tab_util.cc`
`chrome-browser-extensions-extension_tab_util.cc.patch`. Tabs given to Firefox add-ons
carry their container's `cookieStoreId`.
- Needs: `tabs.json`.
- Oblada code: `oblada/browser/firefox_addons/cookie_store_ids.h`.

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
- The menu starts with "Reload" (no longer next to back and forward) and a "Compact mode"
  check item.
- "New container tab ▸" follows the new tab and window items.
- Neither menu offers horizontal tabs.
- Oblada code: `oblada/browser/containers/containers.h`, `containers_menu_model.h`,
  `oblada/browser/l10n.h`.

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
the sidebar stays.

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
- Oblada code: `oblada/browser/about/about_page.h`.

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
Settings' import dialog included: importing is Oblada's own (see `chrome_pages.cc`).

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
version (`chromium/oblada/VERSION`) and the Chromium it's built on.
- Oblada code: the generated `oblada/version.h` (see `oblada/browser/sources.gni`).

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
