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
// Settings' controls, which bind to preferences by name. Its subpages (the
// Barrier's filter lists, its sites) are routes of Settings' own
// (/oblada/filters, /oblada/sites), which Settings passes as `route-path`.
constexpr char kModule[] = R"js(
import {addWebUiListener, removeWebUiListener, sendWithPromise} from 'chrome://resources/js/cr.js';
// cr-button, cr-dialog, cr-input, cr-link-row, cr-textarea and cr-toggle are
// Settings' own, from its bundle (importing them again would define them
// twice).
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {html, LitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

const t = (key) => loadTimeData.getString(key);
const tr = (key, ...args) =>
    args.reduce((s, arg, i) => s.replace('$' + (i + 1), arg), t(key));

// The window's layouts (oblada/browser/ui/sidebar_mode.h), the sidebar's first,
// then those with horizontal tabs, from `HORIZONTAL` on.
const HORIZONTAL = 'horizon';
const LAYOUTS = [
  ['sidebar', 'obladaLayoutSidebar', 'obladaLayoutSidebarDescription'],
  ['scaffold', 'obladaLayoutScaffold', 'obladaLayoutScaffoldDescription'],
  ['expanded_scaffold', 'obladaLayoutExpandedScaffold',
   'obladaLayoutExpandedScaffoldDescription'],
  ['compact', 'obladaLayoutCompact', 'obladaLayoutCompactDescription'],
  ['horizon', 'obladaLayoutHorizon', 'obladaLayoutHorizonDescription'],
  ['horizon_compact', 'obladaLayoutHorizonCompact',
   'obladaLayoutHorizonCompactDescription'],
  ['classic', 'obladaLayoutClassic', 'obladaLayoutClassicDescription'],
];

const options = (keys) => keys.map(([value, key]) => ({value, name: t(key)}));

// The Barrier's defaults (oblada/browser/barrier/barrier.h).
const ADS = () => options([['aggressive', 'barrier.ads.aggressive'],
    ['standard', 'barrier.ads.standard'], ['disabled', 'barrier.ads.disabled']]);
const HTTPS = () => options([['strict', 'barrier.https.strict'],
    ['standard', 'barrier.https.standard'],
    ['disabled', 'barrier.https.disabled']]);
// Chromium's CookieControlsMode: kBlockThirdParty, kIncognitoOnly.
const COOKIES = () => options([[1, 'barrier.cookies.block_third_party'],
    [2, 'barrier.cookies.allow_all']]);
const SENSITIVE_SITES = () => options([['ask', 'settings.privacy.ask'],
    ['always', 'settings.privacy.always'], ['never', 'settings.privacy.never']]);
const WEBRTC = () => options([['default', 'settings.privacy.webrtc.default'],
    ['default_public_and_private_interfaces',
     'settings.privacy.webrtc.public_and_private'],
    ['default_public_interface_only', 'settings.privacy.webrtc.public'],
    ['disable_non_proxied_udp', 'settings.privacy.webrtc.no_udp']]);

const FILTERS_PATH = '/oblada/filters';
const SITES_PATH = '/oblada/sites';

class SettingsObladaPageElement extends LitElement {
  static get properties() {
    return {
      inSearchMode: {type: Boolean},
      routePath: {type: String, attribute: 'route-path'},
      state_: {state: true},
      lists_: {state: true},
      sites_: {state: true},
      confirming_: {state: true},
      showRegional_: {state: true},
      subscribeError_: {state: true},
    };
  }

  constructor() {
    super();
    this.state_ = null;
    this.lists_ = null;
    this.sites_ = [];
    this.confirming_ = false;
    this.showRegional_ = false;
    this.subscribeError_ = false;
  }

  createRenderRoot() {
    return this;
  }

  connectedCallback() {
    super.connectedCallback();
    this.listener_ = addWebUiListener('oblada-barrier-lists-changed',
                                      (lists) => this.lists_ = lists);
    this.stateListener_ = addWebUiListener('oblada-barrier-state-changed',
                                           (state) => this.state_ = state);
    this.refresh_();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    removeWebUiListener(this.listener_);
    removeWebUiListener(this.stateListener_);
  }

  updated(changed) {
    if (changed.has('routePath')) {
      this.refresh_();
    }
  }

  async refresh_() {
    this.state_ = await sendWithPromise('obladaBarrierState');
    if (this.routePath === FILTERS_PATH) {
      this.lists_ = await sendWithPromise('obladaBarrierLists');
    } else if (this.routePath === SITES_PATH) {
      this.sites_ = await sendWithPromise('obladaBarrierSites');
    }
  }

  // Goes to one of Settings' routes, as its own links do.
  go_(path) {
    window.history.pushState({}, '', path);
    window.dispatchEvent(new PopStateEvent('popstate'));
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
        settings-oblada-page .oblada-description,
        settings-oblada-page .oblada-secondary {
          color: var(--cr-secondary-text-color);
          display: block;
        }
        settings-oblada-page .oblada-group {
          color: var(--cr-secondary-text-color);
          font-weight: 500;
          padding-block: 16px 4px;
        }
        settings-oblada-page .oblada-intro {
          padding: 12px var(--cr-section-padding) 4px;
        }
        settings-oblada-page .oblada-danger {
          border: 1px solid var(--cr-fallback-color-error, #b3261e);
          border-radius: 12px;
          margin: 8px var(--cr-section-padding) 16px;
          padding: 12px 16px;
        }
        settings-oblada-page .oblada-danger-title {
          color: var(--cr-fallback-color-error, #b3261e);
          font-weight: 700;
          letter-spacing: 0.08em;
        }
        settings-oblada-page .oblada-danger cr-button {
          margin-top: 12px;
        }
        settings-oblada-page cr-button.oblada-destructive {
          --cr-button-border-color: var(--cr-fallback-color-error, #b3261e);
          --cr-button-text-color: var(--cr-fallback-color-error, #b3261e);
          font-weight: 600;
        }
        settings-oblada-page .oblada-grow {
          flex: 1;
          padding-inline-end: 12px;
        }
        settings-oblada-page cr-textarea {
          --cr-textarea-autogrow-max-height: 400px;
          font-family: monospace;
          width: 100%;
        }
        settings-oblada-page .oblada-subscribe {
          align-items: center;
          display: flex;
          gap: 8px;
          padding: 8px var(--cr-section-padding);
        }
        settings-oblada-page .oblada-subscribe cr-input {
          flex: 1;
        }
        settings-oblada-page .oblada-block {
          padding: 8px var(--cr-section-padding) 16px;
        }
      </style>
      <div class="cr-centered-card-container">
        ${this.routePath === FILTERS_PATH ? this.renderFilters_() :
          this.routePath === SITES_PATH ? this.renderSites_() :
          this.renderMain_()}
      </div>`;
  }

  renderMain_() {
    return html`
      <settings-section page-title="${t('obladaAppearance')}">
        <div class="cr-row first">
          <div class="oblada-grow cr-padded-text">${t('obladaLayout')}</div>
        </div>
        <div class="oblada-list">
          <settings-radio-group pref-key="oblada.sidebar.mode"
              group-aria-label="${t('obladaLayout')}">
            ${LAYOUTS.map(([name, label, description], i) => html`
              ${i === 0 || name === HORIZONTAL ? html`
                <div class="oblada-group">
                  ${t(i === 0 ? 'obladaLayoutSide' : 'obladaLayoutTop')}
                </div>` : ''}
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
      ${this.renderBarrier_()}
      ${this.renderPrivacy_()}
      ${this.renderDanger_()}`;
  }

  renderDropdown_(prefKey, label, menuOptions, description = '') {
    return html`
      <div class="cr-row">
        <div class="oblada-grow cr-padded-text">
          ${label}
          ${description ? html`
            <span class="oblada-secondary">${description}</span>` : ''}
        </div>
        <settings-dropdown-menu pref-key="${prefKey}" label="${label}"
            .menuOptions="${menuOptions}">
        </settings-dropdown-menu>
      </div>`;
  }

  renderBarrier_() {
    const off = this.state_ && !this.state_.on;
    return html`
      <settings-section page-title="${t('barrier.title')}">
        <div class="oblada-intro oblada-secondary">
          ${t('settings.barrier.intro')}
          ${this.state_ ? html`<br>${tr('settings.barrier.blocked_total',
              this.state_.blockedTotal.toLocaleString())}` : ''}
          ${off ? html`<br><b>${t('barrier.off_everywhere')}</b>` : ''}
        </div>
        ${this.renderDropdown_('oblada.barrier.ads', t('barrier.ads'), ADS(),
                               t('settings.barrier.ads_description'))}
        ${this.renderDropdown_('oblada.barrier.https', t('barrier.https'),
                               HTTPS(), t('settings.barrier.https_description'))}
        <div class="cr-row">
          <div class="oblada-grow cr-padded-text">
            ${t('barrier.scripts')}
            <span class="oblada-secondary">
              ${t('settings.barrier.scripts_description')}
            </span>
          </div>
          <cr-toggle aria-label="${t('barrier.scripts')}"
              ?checked="${!!this.state_?.blockScripts}"
              @change="${(e) => sendWithPromise(
                  'obladaBarrierSetBlockScripts', e.detail)}">
          </cr-toggle>
        </div>
        <settings-toggle-button class="hr"
            pref-key="oblada.barrier.fingerprinting"
            label="${t('barrier.fingerprinting')}"
            sub-label="${t('settings.barrier.fingerprinting_description')}">
        </settings-toggle-button>
        ${this.renderDropdown_('profile.cookie_controls_mode',
                               t('barrier.cookies'), COOKIES(),
                               t('settings.barrier.cookies_description'))}
        <settings-toggle-button class="hr" pref-key="oblada.barrier.forget"
            label="${t('settings.barrier.forget')}"
            sub-label="${t('settings.barrier.forget_description')}">
        </settings-toggle-button>
        <settings-toggle-button class="hr" pref-key="oblada.barrier.show_count"
            label="${t('settings.barrier.show_count')}">
        </settings-toggle-button>
        <cr-link-row class="hr" label="${t('barrier.filters')}"
            sub-label="${t('settings.barrier.filters_description')}"
            @click="${() => this.go_(FILTERS_PATH)}">
        </cr-link-row>
        <cr-link-row class="hr" label="${t('settings.barrier.sites')}"
            @click="${() => this.go_(SITES_PATH)}">
        </cr-link-row>
      </settings-section>`;
  }

  renderPrivacy_() {
    const toggle = (pref, label, subLabel = '') => html`
      <settings-toggle-button class="hr" pref-key="${pref}" label="${label}"
          sub-label="${subLabel}">
      </settings-toggle-button>`;
    return html`
      <settings-section page-title="${t('settings.privacy')}">
        ${toggle('oblada.barrier.gpc', t('settings.privacy.gpc'))}
        ${toggle('enable_do_not_track', t('settings.privacy.dnt'))}
        ${toggle('oblada.barrier.strip_params',
                 t('settings.privacy.strip_params'))}
        ${toggle('oblada.barrier.debounce', t('settings.privacy.debounce'))}
        ${toggle('oblada.barrier.de_amp', t('settings.privacy.de_amp'))}
        ${toggle('oblada.barrier.reduce_language',
                 t('settings.privacy.reduce_language'),
                 t('settings.privacy.reduce_language_description'))}
        ${this.renderDropdown_('oblada.barrier.sensitive_sites',
                               t('settings.privacy.sensitive_sites'),
                               SENSITIVE_SITES(),
                               t('settings.privacy.sensitive_sites_description'))}
        ${this.renderDropdown_('webrtc.ip_handling_policy',
                               t('settings.privacy.webrtc'), WEBRTC())}
        <div class="cr-row">
          <div class="oblada-grow cr-padded-text oblada-group">
            ${t('settings.privacy.social')}
          </div>
        </div>
        ${toggle('oblada.barrier.facebook_embeds',
                 t('settings.privacy.facebook'))}
        ${toggle('oblada.barrier.x_embeds', t('settings.privacy.x'))}
        ${toggle('oblada.barrier.linkedin_embeds',
                 t('settings.privacy.linkedin'))}
        ${toggle('oblada.barrier.google_sign_in',
                 t('settings.privacy.google_sign_in'),
                 t('settings.privacy.google_sign_in_description'))}
        <div class="cr-row">
          <div class="oblada-grow cr-padded-text">
            <div class="oblada-group">${t('settings.privacy.startup')}</div>
            <div class="oblada-secondary">
              ${t('settings.privacy.startup_description')}
            </div>
          </div>
          ${this.state_?.startupChanged ? html`
            <cr-button @click="${() => sendWithPromise('obladaBarrierRelaunch')}">
              ${t('settings.danger.relaunch')}
            </cr-button>` : ''}
        </div>
        ${toggle('oblada.barrier.partition_hsts',
                 t('settings.privacy.partition_hsts'),
                 t('settings.privacy.partition_hsts_description'))}
        ${toggle('oblada.barrier.partition_connections',
                 t('settings.privacy.partition_connections'))}
        ${toggle('oblada.barrier.web_apis_off',
                 t('settings.privacy.web_apis_off'),
                 t('settings.privacy.web_apis_off_description'))}
        ${toggle('oblada.barrier.referrer_cap',
                 t('settings.privacy.referrer_cap'))}
        ${toggle('oblada.barrier.cookie_expiry_cap',
                 t('settings.privacy.cookie_expiry_cap'))}
      </settings-section>`;
  }

  // "DANGEROUS": the switch that turns all of the Barrier off.
  renderDanger_() {
    if (!this.state_) {
      return '';
    }
    const {on, onAtStartup} = this.state_;
    return html`
      <div class="oblada-danger">
        <div class="oblada-danger-title">${t('settings.danger')}</div>
        <div class="oblada-secondary">
          ${on ? t('settings.danger.description') : t('settings.danger.off')}
        </div>
        ${on !== onAtStartup ? html`
          <div class="oblada-secondary">${t('settings.danger.relaunch_note')}</div>
          <cr-button @click="${() => sendWithPromise('obladaBarrierRelaunch')}">
            ${t('settings.danger.relaunch')}
          </cr-button>` : ''}
        ${on ? html`
          <cr-button class="oblada-destructive"
              @click="${() => this.confirming_ = true}">
            ${t('settings.danger.turn_off')}
          </cr-button>` : html`
          <cr-button class="action-button" @click="${() => this.setOn_(true)}">
            ${t('settings.danger.turn_on')}
          </cr-button>`}
      </div>
      ${this.confirming_ ? html`
        <cr-dialog show-on-attach @close="${() => this.confirming_ = false}">
          <div slot="title">${t('settings.danger.confirm_title')}</div>
          <div slot="body">${t('settings.danger.description')}</div>
          <div slot="button-container">
            <cr-button class="cancel-button"
                @click="${() => this.confirming_ = false}">
              ${t('settings.cancel')}
            </cr-button>
            <cr-button class="oblada-destructive"
                @click="${() => this.setOn_(false)}">
              ${t('settings.danger.confirm')}
            </cr-button>
          </div>
        </cr-dialog>` : ''}`;
  }

  async setOn_(on) {
    this.confirming_ = false;
    this.state_ = await sendWithPromise('obladaBarrierSetEnabled', on);
  }

  renderFilters_() {
    const lists = this.lists_;
    const updated = (time) => time ?
        tr('settings.filters.updated', new Date(time).toLocaleString()) :
        t('settings.filters.not_yet');
    const listRow = (list) => html`
      <div class="cr-row">
        <div class="oblada-grow cr-padded-text">
          ${list.title}
          <span class="oblada-secondary">
            ${[list.description, updated(list.updated)].filter((t) => t)
                .join(' · ')}
          </span>
        </div>
        <cr-toggle aria-label="${list.title}" ?checked="${list.enabled}"
            @change="${(e) => sendWithPromise('obladaBarrierSetList', list.uuid,
                                              e.detail)}">
        </cr-toggle>
      </div>`;
    return html`
      <settings-subpage page-title="${t('barrier.filters')}">
        <div class="oblada-intro oblada-secondary">
          ${t('settings.filters.credits')}
        </div>
        <div class="cr-row first">
          <div class="oblada-grow cr-padded-text">${t('settings.filters.lists')}</div>
          <cr-button @click="${() => sendWithPromise('obladaBarrierUpdateLists')}">
            ${t('settings.filters.update')}
          </cr-button>
        </div>
        ${lists ? lists.lists.filter((list) => !list.regional).map(listRow) : ''}
        <cr-link-row class="hr" label="${t('settings.filters.regional')}"
            @click="${() => this.showRegional_ = !this.showRegional_}">
        </cr-link-row>
        ${lists && this.showRegional_ ?
            lists.lists.filter((list) => list.regional).map(listRow) : ''}
        <div class="cr-row">
          <div class="oblada-grow cr-padded-text oblada-group">
            ${t('settings.filters.subscriptions')}
          </div>
        </div>
        ${lists ? lists.subscriptions.map((subscription) => html`
          <div class="cr-row">
            <div class="oblada-grow cr-padded-text">
              ${subscription.title || subscription.url}
              <span class="oblada-secondary">
                ${subscription.url} ·
                ${subscription.failed ? t('settings.filters.failed') :
                                        updated(subscription.updated)}
              </span>
            </div>
            <cr-toggle aria-label="${subscription.url}"
                ?checked="${subscription.enabled}"
                @change="${(e) => sendWithPromise(
                    'obladaBarrierSetSubscription', subscription.url,
                    e.detail)}">
            </cr-toggle>
            <cr-button @click="${() => sendWithPromise(
                'obladaBarrierUnsubscribe', subscription.url)}">
              ${t('settings.filters.remove')}
            </cr-button>
          </div>`) : ''}
        <div class="oblada-subscribe">
          <cr-input id="subscribe" type="url"
              placeholder="${t('settings.filters.subscribe_placeholder')}"
              ?invalid="${this.subscribeError_}"
              @input="${() => this.subscribeError_ = false}">
          </cr-input>
          <cr-button @click="${this.subscribe_}">
            ${t('settings.filters.subscribe')}
          </cr-button>
        </div>
        <div class="oblada-block oblada-secondary">
          ${t('settings.filters.subscribe_note')}
        </div>
        <div class="cr-row">
          <div class="oblada-grow cr-padded-text oblada-group">
            ${t('settings.filters.custom')}
            <span class="oblada-secondary">
              ${t('settings.filters.custom_description')}
            </span>
          </div>
        </div>
        <div class="oblada-block">
          <cr-textarea id="customFilters" autogrow rows="6"
              .value="${lists ? lists.customFilters : ''}">
          </cr-textarea>
          <cr-button class="action-button" @click="${this.saveFilters_}">
            ${t('settings.filters.save')}
          </cr-button>
        </div>
      </settings-subpage>`;
  }

  async subscribe_() {
    const input = this.querySelector('#subscribe');
    const ok = await sendWithPromise('obladaBarrierSubscribe', input.value.trim());
    this.subscribeError_ = !ok;
    if (ok) {
      input.value = '';
    }
  }

  saveFilters_() {
    sendWithPromise('obladaBarrierSetCustomFilters',
                    this.querySelector('#customFilters').value);
  }

  renderSites_() {
    return html`
      <settings-subpage page-title="${t('settings.barrier.sites')}">
        ${this.sites_.length ? this.sites_.map((site) => html`
          <div class="cr-row">
            <div class="oblada-grow cr-padded-text">
              ${site.site}
              <span class="oblada-secondary">
                ${site.changes.map((change) => t('settings.sites.' + change))
                      .join(' · ')}
              </span>
            </div>
            <cr-button @click="${async () => {
                await sendWithPromise('obladaBarrierResetSite', site.site);
                this.sites_ = await sendWithPromise('obladaBarrierSites');
              }}">
              ${t('settings.barrier.reset')}
            </cr-button>
          </div>`) : html`
          <div class="oblada-intro oblada-secondary">
            ${t('settings.barrier.sites_none')}
          </div>`}
      </settings-subpage>`;
  }

  // For Settings' search: the page shows when one of its settings mentions the
  // query.
  searchContents(query) {
    const words = query.trim().toLowerCase();
    const texts = [
      'obladaPageTitle', 'obladaAppearance', 'obladaLayout',
      'obladaShowWorkspaceHeader', 'obladaLayoutSide', 'obladaLayoutTop',
      ...LAYOUTS.flatMap(([, label, description]) => [label, description]),
      ...SEARCHABLE,
    ].map((key) => t(key).toLowerCase());
    return Promise.resolve({
      canceled: false,
      matchCount: words ? texts.filter((text) => text.includes(words)).length
                        : 0,
      wasClearSearch: !words,
    });
  }
}

// The Barrier's and privacy settings' strings Settings' search looks in.
const SEARCHABLE = [
  'barrier.title', 'settings.barrier.intro', 'barrier.ads', 'barrier.https',
  'barrier.scripts', 'barrier.fingerprinting', 'barrier.cookies',
  'settings.barrier.forget', 'barrier.filters', 'settings.barrier.sites',
  'settings.privacy', 'settings.privacy.gpc', 'settings.privacy.dnt',
  'settings.privacy.strip_params', 'settings.privacy.debounce',
  'settings.privacy.de_amp', 'settings.privacy.reduce_language',
  'settings.privacy.sensitive_sites', 'settings.privacy.webrtc',
  'settings.privacy.social', 'settings.privacy.google_sign_in',
  'settings.privacy.partition_hsts', 'settings.privacy.partition_connections',
  'settings.privacy.web_apis_off', 'settings.privacy.referrer_cap',
  'settings.privacy.cookie_expiry_cap', 'settings.danger',
];

customElements.define('settings-oblada-page', SettingsObladaPageElement);
)js";

// Oblada's strings the page uses by their own keys (l10n.cc).
constexpr const char* kStrings[] = {
    "barrier.title",
    "barrier.off_everywhere",
    "barrier.ads",
    "barrier.ads.aggressive",
    "barrier.ads.standard",
    "barrier.ads.disabled",
    "barrier.https",
    "barrier.https.strict",
    "barrier.https.standard",
    "barrier.https.disabled",
    "barrier.scripts",
    "barrier.fingerprinting",
    "barrier.cookies",
    "barrier.cookies.block_third_party",
    "barrier.cookies.allow_all",
    "barrier.filters",
    "settings.barrier.intro",
    "settings.barrier.blocked_total",
    "settings.barrier.ads_description",
    "settings.barrier.https_description",
    "settings.barrier.scripts_description",
    "settings.barrier.fingerprinting_description",
    "settings.barrier.cookies_description",
    "settings.barrier.forget",
    "settings.barrier.forget_description",
    "settings.barrier.show_count",
    "settings.barrier.filters_description",
    "settings.barrier.sites",
    "settings.barrier.sites_none",
    "settings.barrier.reset",
    "settings.privacy",
    "settings.privacy.gpc",
    "settings.privacy.dnt",
    "settings.privacy.strip_params",
    "settings.privacy.debounce",
    "settings.privacy.de_amp",
    "settings.privacy.reduce_language",
    "settings.privacy.reduce_language_description",
    "settings.privacy.sensitive_sites",
    "settings.privacy.sensitive_sites_description",
    "settings.privacy.ask",
    "settings.privacy.always",
    "settings.privacy.never",
    "settings.privacy.webrtc",
    "settings.privacy.webrtc.default",
    "settings.privacy.webrtc.public_and_private",
    "settings.privacy.webrtc.public",
    "settings.privacy.webrtc.no_udp",
    "settings.privacy.social",
    "settings.privacy.facebook",
    "settings.privacy.x",
    "settings.privacy.linkedin",
    "settings.privacy.google_sign_in",
    "settings.privacy.startup",
    "settings.privacy.startup_description",
    "settings.privacy.partition_hsts",
    "settings.privacy.partition_hsts_description",
    "settings.privacy.partition_connections",
    "settings.privacy.web_apis_off",
    "settings.privacy.web_apis_off_description",
    "settings.privacy.referrer_cap",
    "settings.privacy.cookie_expiry_cap",
    "settings.privacy.google_sign_in_description",
    "settings.danger",
    "settings.danger.description",
    "settings.danger.off",
    "settings.danger.turn_off",
    "settings.danger.turn_on",
    "settings.danger.relaunch_note",
    "settings.danger.relaunch",
    "settings.danger.confirm_title",
    "settings.danger.confirm",
    "settings.cancel",
    "settings.filters.credits",
    "settings.filters.lists",
    "settings.filters.update",
    "settings.filters.updated",
    "settings.filters.not_yet",
    "settings.filters.regional",
    "settings.filters.subscriptions",
    "settings.filters.subscribe_placeholder",
    "settings.filters.subscribe",
    "settings.filters.subscribe_note",
    "settings.filters.failed",
    "settings.filters.remove",
    "settings.filters.custom",
    "settings.filters.custom_description",
    "settings.filters.save",
    "settings.sites.enabled",
    "settings.sites.disabled",
    "settings.sites.ads_aggressive",
    "settings.sites.ads_standard",
    "settings.sites.ads_disabled",
    "settings.sites.https_strict",
    "settings.sites.https_standard",
    "settings.sites.https_disabled",
    "settings.sites.scripts_blocked",
    "settings.sites.scripts_allowed",
    "settings.sites.fingerprinting_blocked",
    "settings.sites.fingerprinting_allowed",
    "settings.sites.cookies_allowed",
    "settings.sites.cookies_blocked",
    "settings.sites.forget",
    "settings.sites.remember",
    "settings.sites.google_sign_in_allowed",
    "settings.sites.google_sign_in_blocked",
};

}  // namespace

void AddObladaPage(content::WebUIDataSource* source) {
  source->AddString("obladaPageTitle", L10n("settings.oblada"));
  source->AddString("obladaAppearance", L10n("settings.appearance"));
  source->AddString("obladaLayout", L10n("settings.layout"));
  source->AddString("obladaLayoutSide", L10n("settings.layout.side"));
  source->AddString("obladaLayoutTop", L10n("settings.layout.top"));
  // The window's layouts, by name in the page and in Oblada's strings.
  static constexpr std::pair<const char*, const char*> kLayouts[] = {
      {"Sidebar", "sidebar"},
      {"Scaffold", "scaffold"},
      {"ExpandedScaffold", "expanded_scaffold"},
      {"Compact", "compact"},
      {"Horizon", "horizon"},
      {"HorizonCompact", "horizon_compact"},
      {"Classic", "classic"}};
  for (const auto& [name, key] : kLayouts) {
    source->AddString(base::StrCat({"obladaLayout", name}),
                      L10n(base::StrCat({"settings.layout.", key})));
    source->AddString(
        base::StrCat({"obladaLayout", name, "Description"}),
        L10n(base::StrCat({"settings.layout.", key, "_description"})));
  }
  source->AddString("obladaShowWorkspaceHeader",
                    L10n("settings.workspace_header"));
  for (const char* key : kStrings) {
    source->AddString(key, L10n(key));
  }
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
