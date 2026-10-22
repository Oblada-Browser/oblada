#include "oblada/browser/settings/oblada_page.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "content/public/browser/web_ui_data_source.h"
#include "oblada/browser/l10n.h"

namespace oblada::settings {

namespace {

constexpr char kModulePath[] = "oblada/oblada_page.js";

// settings-oblada-page. Its content is in the page's tree, not a shadow root of
// its own, so that Settings' styles apply to it as to its own pages; it uses
// Settings' controls, which bind to preferences by name.
constexpr char kModule[] = R"js(
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {html, LitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

const t = (key) => loadTimeData.getString(key);

// The window's layouts (oblada/browser/ui/sidebar_mode.h).
const LAYOUTS = [
  ['sidebar', 'obladaLayoutSidebar', 'obladaLayoutSidebarDescription'],
  ['scaffold', 'obladaLayoutScaffold', 'obladaLayoutScaffoldDescription'],
  ['compact', 'obladaLayoutCompact', 'obladaLayoutCompactDescription'],
];

class SettingsObladaPageElement extends LitElement {
  static get properties() {
    return {inSearchMode: {type: Boolean}};
  }

  createRenderRoot() {
    return this;
  }

  render() {
    return html`
      <style>
        settings-oblada-page .oblada-list {
          display: block;
          padding-inline-end: var(--cr-section-padding);
          padding-inline-start: var(--cr-section-indent-padding);
        }
        settings-oblada-page controlled-radio-button {
          align-items: center;
          display: flex;
          min-height: var(--cr-section-min-height);
          padding-block: 8px;
        }
        settings-oblada-page .oblada-description {
          color: var(--cr-secondary-text-color);
          display: block;
        }
      </style>
      <div class="cr-centered-card-container">
        <settings-section page-title="${t('obladaAppearance')}">
          <div class="cr-row first">
            <div class="flex cr-padded-text">${t('obladaLayout')}</div>
          </div>
          <div class="oblada-list">
            <settings-radio-group pref-key="oblada.sidebar.mode"
                group-aria-label="${t('obladaLayout')}">
              ${LAYOUTS.map(([name, label, description]) => html`
                <controlled-radio-button name="${name}"
                    pref-key="oblada.sidebar.mode" label="${t(label)}"
                    no-extension-indicator>
                  <span class="oblada-description">${t(description)}</span>
                </controlled-radio-button>`)}
            </settings-radio-group>
          </div>
          <settings-toggle-button class="hr"
              pref-key="oblada.workspaces.show_header"
              label="${t('obladaShowWorkspaceHeader')}">
          </settings-toggle-button>
        </settings-section>
      </div>`;
  }

  // For Settings' search: the page shows when one of its settings mentions the
  // query.
  searchContents(query) {
    const words = query.trim().toLowerCase();
    const texts = [
      'obladaPageTitle', 'obladaAppearance', 'obladaLayout',
      'obladaShowWorkspaceHeader',
      ...LAYOUTS.flatMap(([, label, description]) => [label, description]),
    ].map((key) => t(key).toLowerCase());
    return Promise.resolve({
      canceled: false,
      matchCount: words ? texts.filter((text) => text.includes(words)).length
                        : 0,
      wasClearSearch: !words,
    });
  }
}

customElements.define('settings-oblada-page', SettingsObladaPageElement);
)js";

}  // namespace

void AddObladaPage(content::WebUIDataSource* source) {
  source->AddString("obladaPageTitle", L10n("settings.oblada"));
  source->AddString("obladaAppearance", L10n("settings.appearance"));
  source->AddString("obladaLayout", L10n("settings.layout"));
  // The window's layouts, by name in the page and in Oblada's strings.
  static constexpr std::pair<const char*, const char*> kLayouts[] = {
      {"Sidebar", "sidebar"}, {"Scaffold", "scaffold"}, {"Compact", "compact"}};
  for (const auto& [name, key] : kLayouts) {
    source->AddString(base::StrCat({"obladaLayout", name}),
                      L10n(base::StrCat({"settings.layout.", key})));
    source->AddString(
        base::StrCat({"obladaLayout", name, "Description"}),
        L10n(base::StrCat({"settings.layout.", key, "_description"})));
  }
  source->AddString("obladaShowWorkspaceHeader",
                    L10n("settings.workspace_header"));
  source->SetRequestFilter(
      base::BindRepeating(
          [](const std::string& path) { return path == kModulePath; }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(
                    std::string(kModule)));
          }));
}

}  // namespace oblada::settings
