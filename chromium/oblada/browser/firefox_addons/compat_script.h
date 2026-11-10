#ifndef OBLADA_BROWSER_FIREFOX_ADDONS_COMPAT_SCRIPT_H_
#define OBLADA_BROWSER_FIREFOX_ADDONS_COMPAT_SCRIPT_H_

#include <string_view>

namespace oblada::firefox_addons {

// Added to every converted Firefox add-on and run first in each of its contexts
// (background, content scripts, its pages), to cover what Chromium names or requires
// differently. Chromium already has the `browser` namespace and lets
// runtime.onMessage listeners reply with a promise, as Firefox does.
inline constexpr char kCompatScriptName[] = "oblada-compat.js";

inline constexpr std::string_view kCompatScript = R"js(// Oblada: Firefox compatibility.
(() => {
  'use strict';
  const g = globalThis;
  if (g.__obladaFirefoxCompat || !g.chrome?.runtime?.id) return;
  g.__obladaFirefoxCompat = true;
  const namespaces = [...new Set([g.chrome, g.browser].filter(Boolean))];

  const define = (target, name, value) => {
    try {
      if (target && target[name] === undefined) {
        Object.defineProperty(target, name, { value, configurable: true, writable: true });
      }
    } catch {}
  };
  const replace = (target, name, wrap) => {
    try {
      const original = target?.[name];
      if (typeof original === 'function') target[name] = wrap(original.bind(target));
    } catch {}
  };
  const noopEvent = () => ({ addListener() {}, removeListener() {}, hasListener: () => false });

  // Firefox calls contextMenus "menus", and accepts more properties and contexts.
  const CONTEXTS = new Set(['all', 'page', 'frame', 'selection', 'link', 'editable', 'image',
    'video', 'audio', 'launcher', 'browser_action', 'page_action', 'action', 'tab']);
  const menuProperties = (props) => {
    if (!props || typeof props !== 'object') return props;
    const { icons, command, viewTypes, ...rest } = props;
    if (Array.isArray(rest.contexts)) {
      const contexts = rest.contexts.filter((c) => CONTEXTS.has(c));
      // Only Firefox-only places (bookmarks, tools menu...): never shown here.
      if (!contexts.length) return { ...rest, contexts: ['page'], visible: false };
      rest.contexts = contexts;
    }
    return rest;
  };
  for (const ns of namespaces) {
    const contextMenus = ns.contextMenus;
    if (!contextMenus) continue;
    const menus = Object.create(contextMenus);
    menus.create = (props, callback) => contextMenus.create(menuProperties(props), callback);
    menus.update = (id, props, callback) => contextMenus.update(id, menuProperties(props), callback);
    menus.onShown = noopEvent();
    menus.onHidden = noopEvent();
    menus.refresh = () => Promise.resolve();
    menus.getTargetElement = () => null;
    menus.overrideContext = () => {};
    define(ns, 'menus', menus);
  }

  // Firefox themes: nothing to read or change here.
  for (const ns of namespaces) {
    define(ns, 'theme', {
      getCurrent: () => Promise.resolve({ colors: null, images: null, properties: null }),
      update: () => Promise.resolve(),
      reset: () => Promise.resolve(),
      onUpdated: noopEvent(),
    });
  }

  // Firefox makes a notification's type and icon optional; Chromium requires them.
  const icons = g.chrome.runtime.getManifest().icons ?? {};
  const icon = Object.entries(icons).sort((a, b) => b[0] - a[0]).map(([, path]) => path)
    .find((path) => !/\.svg$/i.test(path));
  const defaultIcon = icon ? g.chrome.runtime.getURL(icon)
    : 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=';
  for (const ns of namespaces) {
    replace(ns.notifications, 'create', (create) => (...args) => {
      const at = typeof args[0] === 'object' && args[0] !== null ? 0 : 1;
      if (args[at] && typeof args[at] === 'object') {
        args[at] = { type: 'basic', iconUrl: defaultIcon, ...args[at] };
      }
      return create(...args);
    });
  }

  // Removed from Chromium, still used by Firefox add-ons.
  for (const ns of namespaces) {
    if (ns.extension) define(ns.extension, 'getURL', (path) => ns.runtime.getURL(path));
  }

  // Firefox accepts properties Chromium doesn't know, and Chromium refuses those:
  // keep the ones it knows (cookieStoreId is Oblada's, as in Firefox).
  const only = (keys) => {
    const known = new Set(keys);
    return (props) => props && typeof props === 'object' && !Array.isArray(props)
      ? Object.fromEntries(Object.entries(props).filter(([key]) => known.has(key)))
      : props;
  };
  const filters = {
    tabs: {
      create: [0, only(['windowId', 'index', 'url', 'active', 'selected', 'pinned',
        'openerTabId', 'splitWithTabId', 'cookieStoreId'])],
      query: [0, only(['active', 'pinned', 'audible', 'muted', 'highlighted', 'discarded',
        'frozen', 'autoDiscardable', 'currentWindow', 'lastFocusedWindow', 'status', 'title',
        'url', 'groupId', 'splitViewId', 'windowId', 'windowType', 'index', 'cookieStoreId'])],
      update: [1, only(['url', 'active', 'highlighted', 'selected', 'pinned', 'muted',
        'openerTabId', 'autoDiscardable'])],
    },
    windows: {
      create: [0, only(['url', 'tabId', 'left', 'top', 'width', 'height', 'focused',
        'incognito', 'type', 'state', 'setSelfAsOpener'])],
    },
  };
  for (const ns of namespaces) {
    for (const [api, functions] of Object.entries(filters)) {
      for (const [name, [at, filter]] of Object.entries(functions)) {
        replace(ns[api], name, (original) => (...args) => {
          // tabs.update's tab id is optional.
          const index = name === 'update' && typeof args[0] === 'object' ? 0 : at;
          args[index] = filter(args[index]);
          return original(...args);
        });
      }
    }
  }

  // Cookies: Oblada knows Firefox's cookie store ids ("firefox-default", containers'
  // "firefox-container-<n>"); the cookies it returns say Chrome's "0" and "1".
  const firefoxStore = (id) => (id === '0' ? 'firefox-default' : id === '1' ? 'firefox-private' : id);
  const firefoxCookies = (value) => {
    if (Array.isArray(value)) return value.map(firefoxCookies);
    if (value && typeof value === 'object' && typeof value.storeId === 'string') {
      return { ...value, storeId: firefoxStore(value.storeId) };
    }
    return value;
  };
  for (const ns of namespaces) {
    for (const name of ['get', 'getAll', 'set', 'remove']) {
      replace(ns.cookies, name, (original) => (...args) => {
        const callback = typeof args[args.length - 1] === 'function' ? args.pop() : null;
        const result = Promise.resolve(original(...args)).then(firefoxCookies);
        if (!callback) return result;
        result.then(callback, () => callback());
      });
    }
  }

  // Message names Chromium doesn't allow were renamed ("a-b" is "a_b").
  for (const ns of namespaces) {
    replace(ns.i18n, 'getMessage', (getMessage) => (name, ...rest) =>
      getMessage(typeof name === 'string' ? name.replace(/[^A-Za-z0-9_]/g, '_') : name, ...rest));
  }

  // Firefox's contentScripts.register(), which Chromium has only in Manifest V3 (and for
  // files only): the scripts run in each matching frame as it commits, with
  // webNavigation, which add-ons using it ask for (uBlock Origin). Main-world scripts are
  // added to the page as a <script>, which the page's CSP can refuse.
  const { tabs, webNavigation } = g.chrome;
  if (tabs?.executeScript && webNavigation?.onCommitted) {
    const escape = (text) => text.replace(/[.+?^${}()|[\]\\]/g, '\\$&');
    const glob = (pattern) => new RegExp('^' + pattern.replace(/[.+^${}()|[\]\\]/g, '\\$&')
      .replace(/\*/g, '.*').replace(/\?/g, '.') + '$');
    const matchPattern = (pattern) => {
      if (pattern === '<all_urls>') return /^(?:https?|wss?|file|ftp):/;
      const [, scheme, host, path] = /^(\*|[a-z][a-z0-9+.-]*):\/\/([^/]*)(\/.*)$/.exec(pattern) ?? [];
      if (!path) return /$^/;
      const hosts = host === '*' ? '[^/]*'
        : host.startsWith('*.') ? `(?:[^/]*\\.)?${escape(host.slice(2))}(?::\\d+)?`
        : `${escape(host)}(?::\\d+)?`;
      return new RegExp(`^${scheme === '*' ? '(?:https?|wss?)' : escape(scheme)}://${hosts}` +
        `${escape(path).replace(/\*/g, '.*')}$`);
    };
    const mainWorld = (code) => `(() => { const script = document.createElement('script');
      script.textContent = ${JSON.stringify(code)};
      (document.head || document.documentElement).append(script); script.remove(); })();`;
    const registered = new Set();
    const run = (script, { tabId, frameId }) => {
      const where = { frameId, runAt: script.runAt, matchAboutBlank: script.matchAboutBlank };
      const done = () => void g.chrome.runtime.lastError;
      for (const { code, file } of script.css) {
        tabs.insertCSS(tabId, { ...where, ...(file ? { file } : { code }) }, done);
      }
      for (const { code, file } of script.js) {
        tabs.executeScript(tabId, { ...where, ...(file ? { file }
          : { code: script.world === 'MAIN' ? mainWorld(code) : code }) }, done);
      }
    };
    const onCommitted = (details) => {
      const url = details.url.split('#')[0];
      for (const script of registered) {
        // As in Firefox, not in documents there before it was registered.
        if (details.timeStamp < script.since ||
            (details.frameId !== 0 && !script.allFrames) ||
            !script.matches.some((re) => re.test(url)) ||
            script.excludeMatches.some((re) => re.test(url)) ||
            (script.includeGlobs.length && !script.includeGlobs.some((re) => re.test(url))) ||
            script.excludeGlobs.some((re) => re.test(url))) {
          continue;
        }
        if (script.cookieStoreId === undefined) {
          run(script, details);
        } else {
          tabs.get(details.tabId, (tab) => {
            const ids = [script.cookieStoreId].flat();
            if (!g.chrome.runtime.lastError && ids.includes(tab?.cookieStoreId)) run(script, details);
          });
        }
      }
    };
    const register = (options) => {
      const script = {
        js: options.js ?? [],
        css: options.css ?? [],
        matches: (options.matches ?? []).map(matchPattern),
        excludeMatches: (options.excludeMatches ?? []).map(matchPattern),
        includeGlobs: (options.includeGlobs ?? []).map(glob),
        excludeGlobs: (options.excludeGlobs ?? []).map(glob),
        allFrames: !!options.allFrames,
        matchAboutBlank: !!(options.matchAboutBlank || options.matchOriginAsFallback),
        runAt: options.runAt ?? 'document_idle',
        world: options.world,
        cookieStoreId: options.cookieStoreId,
        since: Date.now(),
      };
      if (!registered.size) webNavigation.onCommitted.addListener(onCommitted);
      registered.add(script);
      return Promise.resolve({
        unregister: () => {
          registered.delete(script);
          if (!registered.size) webNavigation.onCommitted.removeListener(onCommitted);
          return Promise.resolve();
        },
      });
    };
    for (const ns of namespaces) define(ns, 'contentScripts', { register });
  }

  // Firefox's runtime.getBrowserInfo().
  const version = /Chrome\/([\d.]+)/.exec(g.navigator?.userAgent ?? '')?.[1] ?? '';
  for (const ns of namespaces) {
    define(ns.runtime, 'getBrowserInfo', () =>
      Promise.resolve({ name: 'Oblada', vendor: 'Oblada', version, buildID: '' }));
  }
})();
)js";

}  // namespace oblada::firefox_addons

#endif  // OBLADA_BROWSER_FIREFOX_ADDONS_COMPAT_SCRIPT_H_
