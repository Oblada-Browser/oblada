#!/usr/bin/env node
// Checks Oblada's Chromium patches in a real build: runs out/Oblada headless (it never shows a
// window or takes focus) with a throwaway profile and inspects it over the DevTools protocol.
//
//   node chromium/scripts/smoke.mjs [path/to/Oblada.app]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { defaultApp, launch as launchOblada, silentWav, sleep, snapshot, until, watchFocus } from './lib/oblada.mjs';

const app = process.argv[2] ?? defaultApp;
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'oblada-smoke-'));
const profile = path.join(tmp, 'profile');
// Where Oblada writes snapshots of its window (see lib/oblada.mjs snapshot()).
const snapshots = path.join(tmp, 'snapshots');
fs.mkdirSync(snapshots);

// A Manifest V2 extension with a blocking webRequest listener, the API MV3 removed.
const extension = path.join(tmp, 'mv2');
fs.mkdirSync(extension);
fs.writeFileSync(path.join(extension, 'manifest.json'), JSON.stringify({
  manifest_version: 2,
  name: 'Oblada MV2 check',
  version: '1',
  background: { scripts: ['background.js'] },
  permissions: ['webRequest', 'webRequestBlocking', '<all_urls>'],
}));
fs.writeFileSync(path.join(extension, 'background.js'), `
chrome.webRequest.onBeforeRequest.addListener(
  () => ({ cancel: true }), { urls: ['*://oblada-mv2-check.test/*'] }, ['blocking']);
// The tab each test page loaded in, by address.
self.requestTabs = {};
chrome.webRequest.onBeforeRequest.addListener(
  (details) => { requestTabs[details.url] = details.tabId; }, { urls: ['http://127.0.0.1/*'] });
`);

// chrome://omnibox is one of the debug pages, which are off unless enabled in Local State.
fs.mkdirSync(path.join(profile, 'Default'), { recursive: true });
fs.writeFileSync(path.join(profile, 'Local State'), JSON.stringify({ internal_only_uis_enabled: true }));
// Downloads go to the test's own folder, not the user's.
const downloads = path.join(tmp, 'downloads');
fs.mkdirSync(downloads);
fs.writeFileSync(path.join(profile, 'Default', 'Preferences'), JSON.stringify({
  download: { default_directory: downloads, prompt_for_download: false },
  savefile: { default_directory: downloads },
}));

// A Firefox add-on, written the way Firefox accepts and Chromium doesn't.
// A web page to put in containers, and the Firefox add-on's files and updates.
let offerUpdate = false;
const server = http.createServer((req, res) => {
  const xpiFile = /^\/(oblada-check-[\d.]+\.xpi)$/.exec(req.url)?.[1];
  if (xpiFile) {
    res.setHeader('Content-Type', 'application/x-xpinstall');
    res.end(fs.readFileSync(path.join(tmp, xpiFile)));
    return;
  }
  if (req.url === '/updates.json') {
    res.setHeader('Content-Type', 'application/json');
    res.end(JSON.stringify({ addons: { 'oblada-check@example.com': { updates: offerUpdate
      ? [{ version: '1.0', update_link: `${site}oblada-check-1.0.xpi` },
         { version: '1.1', update_link: `${site}oblada-check-1.1.xpi` }]
      : [] } } }));
    return;
  }
  // Music (silence) for the media controls.
  if (req.url === '/silence.wav') {
    res.setHeader('Content-Type', 'audio/wav');
    res.end(silentWav());
    return;
  }
  // Mozilla's list of tracking parameters, as its server answers (see cleanLinkChecks).
  if (req.url === '/mozilla-list.json') {
    res.setHeader('Content-Type', 'application/json');
    res.end(JSON.stringify({ data: [{ stripList: ['oblada_mozilla_tracker'], allowList: [] }] }));
    return;
  }
  if (req.url === '/?peek-from') {
    res.setHeader('Content-Type', 'text/html');
    res.end('<!doctype html><a href="/?peek" style="display:block;font-size:40px;padding:40px">peek</a>');
    return;
  }
  if (req.url === '/?music') {
    res.setHeader('Content-Type', 'text/html');
    res.end('<!doctype html><title>Oblada test music</title><audio src="/silence.wav" loop></audio>');
    return;
  }
  res.setHeader('Content-Type', 'text/html');
  res.end('<!doctype html><title>Oblada test page</title><p>test page</p>');
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const site = `http://127.0.0.1:${server.address().port}/`;

const addon = path.join(tmp, 'firefox-addon');
fs.mkdirSync(addon);
fs.writeFileSync(path.join(addon, 'manifest.json'), `{
  // Firefox allows comments here.
  "manifest_version": 2,
  "name": "Oblada Firefox check",
  "version": "1.0",
  "browser_specific_settings": { "gecko": { "id": "oblada-check@example.com", "update_url": "${site}updates.json" } },
  "permissions": ["menus", "storage", "dns", "tabs", "cookies", "contextualIdentities", "<all_urls>"],
  "icons": { "48": "icon.svg" },
  "background": { "scripts": ["background.js"] },
  "content_scripts": [{ "matches": ["http://127.0.0.1/*"], "js": ["content.js"] }],
  "browser_action": { "default_popup": "popup.html", "default_icon": "icon.svg" },
  "sidebar_action": { "default_panel": "popup.html" }
}`);
fs.writeFileSync(path.join(addon, 'icon.svg'), '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"/>');
fs.writeFileSync(path.join(addon, 'popup.html'), '<!doctype html><html><head><title>Popup</title></head><body>Popup</body></html>');
fs.writeFileSync(path.join(addon, 'background.js'), `
browser.menus.create({ id: 'oblada-check', title: 'Oblada check', contexts: ['page', 'tools_menu'] });
// Containers, as Multi-Account Containers uses them.
async function containersTest(origin) {
  const events = [];
  const record = (type) => (info) => events.push(type + ':' + info.contextualIdentity.name);
  browser.contextualIdentities.onCreated.addListener(record('created'));
  browser.contextualIdentities.onUpdated.addListener(record('updated'));
  browser.contextualIdentities.onRemoved.addListener(record('removed'));
  const created = await browser.contextualIdentities.create({ name: 'Oblada CI', color: 'purple', icon: 'briefcase' });
  const listed = (await browser.contextualIdentities.query({})).map((i) => i.cookieStoreId);
  const tab = await browser.tabs.create({ cookieStoreId: created.cookieStoreId, url: origin + '/?ci-tab', active: false });
  const inContainer = await browser.tabs.query({ cookieStoreId: created.cookieStoreId });
  const updated = await browser.contextualIdentities.update(created.cookieStoreId, { name: 'Oblada CI 2' });
  // Cookie stores as in Firefox, containers' too; Chrome-style ids made into
  // Firefox's form (Violentmonkey makes "0-default") open in the default one.
  const stores = (await browser.cookies.getAllCookieStores()).map((s) => s.id);
  const containerCookies = await browser.cookies.getAll({ storeId: created.cookieStoreId }).then((c) => Array.isArray(c), (e) => e.message);
  const lenient = await browser.tabs.create({ cookieStoreId: '0-default', url: origin + '/?ci-lenient', active: false });
  await browser.tabs.remove(lenient.id);
  await browser.tabs.remove(tab.id);
  const removed = await browser.contextualIdentities.remove(created.cookieStoreId);
  let error = '';
  try { await browser.contextualIdentities.get(created.cookieStoreId); } catch (e) { error = e.message; }
  await new Promise((resolve) => setTimeout(resolve, 300));
  return {
    created: created.cookieStoreId.startsWith('firefox-container-'),
    listed: listed.includes(created.cookieStoreId),
    tab: tab.cookieStoreId === created.cookieStoreId,
    query: inContainer.length,
    updated: updated.name,
    removed: removed.name,
    gone: error.startsWith('Invalid contextual identity'),
    events,
    stores: stores[0] === 'firefox-default' && stores.includes(created.cookieStoreId),
    containerCookies,
    lenient: lenient.cookieStoreId,
  };
}
browser.runtime.onMessage.addListener(async (message) => message.containers
  ? containersTest(message.containers)
  : {
    reply: message.ping + '-pong',
    menus: typeof browser.menus?.create,
    theme: typeof browser.theme?.getCurrent,
  });
`);
fs.writeFileSync(path.join(addon, 'content.js'), `
browser.runtime.sendMessage({ ping: 'oblada' }).then((reply) => {
  document.documentElement.dataset.obladaFirefox = JSON.stringify(reply);
});
if (location.search === '?containers-test') {
  browser.runtime.sendMessage({ containers: location.origin }).then(
    (reply) => { document.documentElement.dataset.obladaContainers = JSON.stringify(reply); },
    (error) => { document.documentElement.dataset.obladaContainers = 'error: ' + error.message; });
}
`);
execFileSync('zip', ['-qr', path.join(tmp, 'oblada-check-1.0.xpi'), '.'], { cwd: addon });
// Its next version, which Oblada should find on its own.
const manifest = path.join(addon, 'manifest.json');
fs.writeFileSync(manifest, fs.readFileSync(manifest, 'utf8').replace('"version": "1.0"', '"version": "1.1"'));
execFileSync('zip', ['-qr', path.join(tmp, 'oblada-check-1.1.xpi'), '.'], { cwd: addon });


const results = [];
const check = (name, ok, detail = '') => {
  results.push(ok);
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `  (${detail})` : ''}`);
};

const launch = (args = []) => launchOblada({
  app,
  profile,
  args: [`--load-extension=${extension}`, '--oblada-auto-confirm-addon-install',
         '--oblada-addon-update-interval=2', `--oblada-snapshot-dir=${snapshots}`,
         `--oblada-mozilla-list-url=${site}mozilla-list.json`, ...args],
});

async function coreChecks(o) {
  // chrome://omnibox runs the address bar's real autocomplete on typed text and lists each
  // suggestion with the address it opens.
  const omnibox = await o.open('chrome://omnibox');
  const field = "document.querySelector('omnibox-input').shadowRoot.querySelector('#input-text')";
  await until('omnibox page', () => o.evaluate(`!!(${field.replace('.shadowRoot', '?.shadowRoot')})`, omnibox));
  const type = async (input, expected) => {
    await o.evaluate(`{ const f = ${field}; f.value = ${JSON.stringify(input)};
      f.dispatchEvent(new Event('input', { bubbles: true })); }`, omnibox);
    return until(`suggestions for ${input}`, async () => {
      const t = await o.text(omnibox);
      return expected.every((e) => t.includes(e)) && t;
    }, 5000).catch(() => false);
  };
  check('typing oblada://sett suggests oblada://settings, opening chrome://settings',
    !!await type('oblada://sett', ['oblada://settings', 'chrome://settings/']));
  check('typing oblada:// opens the page list', !!await type('oblada://', ['chrome://about/']));

  const versionId = (await o.send('Target.createTarget', { url: 'chrome://version' })).targetId;
  {
    const version = (file) => fs.readFileSync(new URL(file, import.meta.url), 'utf8').trim();
    const chromiumVersion = version('../CHROMIUM_VERSION');
    const s = await o.attach(versionId);
    await until('version page', async () => (await o.text(s)).includes(chromiumVersion));
    const body = await o.text(s);
    // Chromium only for the project itself and the version Oblada is built on, next to
    // Oblada's own (chromium/oblada/VERSION).
    check('product is named Oblada, with its own version',
      /Oblada/.test(body) &&
        body.includes(`${version('../oblada/VERSION')} (Chromium ${chromiumVersion})`) &&
        !/\bChromium\b(?! ?OS)/.test(body.replace(/Chromium (open[- ]source|project|\d)/gi, '')),
      body.split('\n').map((l) => l.trim()).filter((l) => /Oblada|Chromium/.test(l)).slice(0, 3).join(' | '));
    // Oblada's logo (its version, 141 points wide, not Chromium's 171), and no "(Developer
    // Build)": releases are built that way too.
    const page = await o.evaluate(`import('chrome://resources/js/load_time_data.js').then(({loadTimeData}) => ({
      logo: document.querySelector('#logo img').naturalWidth,
      unofficial: document.body.innerText.includes(loadTimeData.getString('official')),
    }))`, s);
    check('the version page has Oblada\'s logo and no build label', page.logo === 141 && !page.unofficial,
      `logo ${page.logo} wide, build label ${page.unofficial ? 'shown' : 'hidden'}`);
    // Without a <title>, the tab shows its address: chrome:// must be displayed as oblada://.
    const title = async () => (await o.pages()).find((p) => p.targetId === versionId)?.title;
    const before = await title();
    await o.evaluate('document.title = ""', s);
    const shown = await until('title', async () => {
      const t = await title();
      return t !== before && t;
    }).catch(() => before);
    check('chrome:// is displayed as oblada://', shown.startsWith('oblada://version'), shown);
  }
  {
    const s = await o.open('chrome://about');
    const body = await until('page list', async () => {
      const t = await o.text(s);
      return t.includes('oblada://settings') && t;
    }).catch(() => o.text(s));
    check('page list shows oblada:// pages', body.includes('List of Oblada URLs') && body.includes('oblada://settings'));
  }

  const prefsSession = await o.open('chrome://prefs-internals');
  // The page is raw JSON, shown through Chrome's JSON viewer: read the JSON itself.
  const prefs = await until('prefs', () =>
    o.evaluate('JSON.parse(document.querySelector("pre")?.textContent || "null")', prefsSession));
  check('vertical tabs are on by default', prefs.vertical_tabs?.enabled?.value === true,
    JSON.stringify(prefs.vertical_tabs?.enabled));

  const background = await until('MV2 background page', async () =>
    (await o.pages()).find((p) => p.type === 'background_page'
      && p.url.endsWith('/_generated_background_page.html')),
  ).catch(() => null);
  check('MV2 extension runs its background page', !!background);
  const { errorText } = await o.send('Page.navigate', { url: 'http://oblada-mv2-check.test/' }, prefsSession);
  check('MV2 blocking webRequest works', errorText === 'net::ERR_BLOCKED_BY_CLIENT', errorText);

  // Chrome extensions see tabs as in Chrome: some, written for Chrome and Firefox,
  // would take Firefox's cookieStoreId for being in Firefox (Violentmonkey did).
  if (background) {
    const chromeTabs = await o.evaluate(`new Promise((resolve) => chrome.tabs.query({},
      (tabs) => resolve(JSON.stringify(tabs.map((t) => 'cookieStoreId' in t)))))`, await o.attach(background.targetId));
    check('Chrome extensions see tabs without Firefox\'s cookieStoreId', chromeTabs && !chromeTabs.includes('true'), chromeTabs);
  }
}

// A tab dragged from the sidebar onto the page goes next to it, as in Zen (Oblada
// simulates the pointer, see oblada/browser/debug/window_snapshots.cc).
async function splitChecks(o) {
  const tabs = [];
  for (const name of ['split-a', 'split-b']) {
    tabs.push((await o.send('Target.createTarget', { url: `${site}?${name}` })).targetId);
  }
  await sleep(1500);
  const shot = path.join(tmp, 'split');
  // The tab before the last one (the active one), onto the right half of the page.
  await snapshot(snapshots, `${shot}-drag.png`, 'drag:-2:0.8,0.5');
  await snapshot(snapshots, `${shot}-drop.png`, 'drop');
  // The pages' widths, from the layout of the window's views.
  const widths = (file) => fs.readFileSync(file, 'utf8').split('\n')
    .map((l) => /^\s*ContentsContainerView \d+,\d+ (\d+)x\d+$/.exec(l)?.[1])
    .filter(Boolean).map(Number);
  const split = widths(`${shot}-drop.layout.txt`);
  check('a tab dragged onto the page goes next to it (split view)',
    split.length === 2 && Math.abs(split[0] - split[1]) < 20, JSON.stringify(split));
  // A pane's grip dragged onto the sidebar makes them whole pages again.
  await snapshot(snapshots, `${shot}-separate.png`, 'grip:0:-0.1,0.3');
  const separate = widths(`${shot}-separate.layout.txt`);
  check('a split pane dragged by its grip onto the sidebar becomes a page again',
    separate.length === 1, JSON.stringify(separate));
  for (const targetId of tabs) await o.send('Target.closeTarget', { targetId });
}

// A link force-clicked opens in a peek, a card over its page, which extensions see as
// the tab it may become (see oblada/browser/peek/peek.h).
async function peekChecks(o) {
  const background = (await o.pages()).find((p) => p.type === 'background_page'
    && p.url.endsWith('/_generated_background_page.html'));
  const extension = await o.attach(background.targetId);
  const fromId = (await o.send('Target.createTarget', { url: `${site}?peek-from` })).targetId;
  const from = await o.attach(fromId);
  await until('page with a link', () => o.evaluate('document.readyState === "complete"', from));
  const link = JSON.parse(await o.evaluate('JSON.stringify(document.querySelector("a").getBoundingClientRect())', from));
  await o.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: link.x + 20, y: link.y + 20 }, from);
  await sleep(500);
  await snapshot(snapshots, path.join(tmp, 'peek.png'), 'force-click wait:1500');
  const peek = (await o.pages()).find((p) => p.type === 'page' && p.url === `${site}?peek`);
  const tabId = await o.evaluate(`requestTabs[${JSON.stringify(`${site}?peek`)}] ?? -1`, extension);
  const ran = tabId > 0 && await o.evaluate(`new Promise((resolve) => chrome.tabs.executeScript(${tabId},
    { code: 'document.title' }, (r) => resolve(chrome.runtime.lastError?.message ?? r[0])))`, extension);
  check('extensions see a peek as a tab (its requests, tabs.executeScript)',
    tabId > 0 && ran === 'Oblada test page', `tab ${tabId}, ${ran}`);
  // Fullscreen is for tabs: asked for in a peek, its page becomes one.
  const s = peek && await o.attach(peek.targetId);
  const full = s && await o.evaluate(`Promise.race([
    document.documentElement.requestFullscreen().then(() => 'ok', String),
    new Promise((resolve) => setTimeout(() => resolve('no answer'), 3000))])`, s, true);
  const fullscreen = s && await o.evaluate('!!document.fullscreenElement', s);
  const index = tabId > 0 && await o.evaluate(`new Promise((resolve) =>
    chrome.tabs.get(${tabId}, (tab) => resolve(tab?.index ?? -1)))`, extension);
  check('fullscreen asked for in a peek makes it a tab, in fullscreen',
    full === 'ok' && fullscreen && index >= 0, `${full}, tab at ${index}`);
  if (fullscreen) await o.evaluate('document.exitFullscreen()', s);
  await sleep(500);
  for (const targetId of [peek?.targetId, fromId].filter(Boolean)) {
    await o.send('Target.closeTarget', { targetId });
  }
}

// "Copy link" (the site panel's, IDC_COPY_URL) copies the page's link without its
// tracking parameters, nor those of a link in its parameters (see
// oblada/browser/clean_links/clean_links.h), Mozilla's list's among them, which
// Oblada downloads from this test's server. Headless, the clipboard is Oblada's own.
async function cleanLinkChecks(o) {
  const inner = (query) => encodeURIComponent(`http://example.com/?${query}`);
  const url = `${site}?clean-links&utm_source=news&id=5&url=${inner('utm_medium=x&q=1')}&FBCLID=x&oblada_mozilla_tracker=1#top`;
  const expected = `${site}?clean-links&id=5&url=${inner('q=1')}#top`;
  const { targetId } = await o.send('Target.createTarget', { url });
  const s = await o.attach(targetId);
  await until('page with tracking parameters', () => o.evaluate('document.readyState === "complete"', s));
  await snapshot(snapshots, path.join(tmp, 'clean-link.png'), '34071 wait:300');
  await o.send('Browser.grantPermissions', { origin: new URL(site).origin, permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  await o.send('Emulation.setFocusEmulationEnabled', { enabled: true }, s);
  const copied = await o.evaluate('navigator.clipboard.readText()', s, true).catch((e) => e.message);
  check('"Copy link" copies the page\'s link without its tracking parameters', copied === expected, copied);
  await o.send('Target.closeTarget', { targetId });
}

// Music playing in a tab other than the one shown gets a card at the bottom of the
// sidebar, as in Zen, whose buttons control it (see oblada/browser/ui/media_controls.h).
async function mediaChecks(o) {
  // The tabs as the MV2 extension sees them, e.g. which one is shown.
  const background = (await o.pages()).find((p) => p.type === 'background_page'
    && p.url.endsWith('/_generated_background_page.html'));
  const extension = background && await o.attach(background.targetId);
  const tabs = (body) => o.evaluate(`new Promise((resolve) => chrome.tabs.query({}, (tabs) => {
    const music = tabs.find((t) => t.url?.endsWith('?music'));
    try { resolve(${body}); } catch (e) { resolve(String(e)); } }))`, extension);
  const music = (await o.send('Target.createTarget', { url: `${site}?music` })).targetId;
  const s = await o.attach(music);
  await until('music page', () => o.evaluate('document.readyState === "complete"', s));
  const play = () => o.send('Runtime.evaluate',
    { expression: 'document.querySelector("audio").play()', awaitPromise: true, userGesture: true }, s);
  await play();
  const other = (await o.send('Target.createTarget', { url: `${site}?after-music` })).targetId;
  await sleep(1500);
  const shot = path.join(tmp, 'media');
  const cards = (name) => fs.readFileSync(`${shot}-${name}.layout.txt`, 'utf8').split('\n')
    .filter((l) => /^\s*MediaCard \d+,\d+ /.test(l)).length;
  await snapshot(snapshots, `${shot}-card.png`);
  check('music playing in another tab gets a card at the bottom of the sidebar',
    cards('card') === 1, `${cards('card')} cards`);

  // Stacked, a card shows its row of buttons: previous, play/pause, next, sound.
  const paused = () => o.evaluate('document.querySelector("audio").paused', s);
  await snapshot(snapshots, `${shot}-pause.png`, 'click:CardButton:1');
  check("a card's button pauses its music", await until('paused', paused, 5000).catch(() => false));
  // (Its player, which stays in its media session; a muted tab's media would leave it.)
  await snapshot(snapshots, `${shot}-mute.png`, 'click:CardButton:3');
  const muted = await until('muted',
    () => o.evaluate('document.querySelector("audio").muted', s), 5000).catch(() => false);
  check("a card's sound button mutes it, and the card stays", muted && cards('mute') === 1,
    `${cards('mute')} cards`);

  // Clicking it shows its tab, which has no card while shown.
  await snapshot(snapshots, `${shot}-show.png`, 'click:MediaCard');
  const shown = await until('music tab shown', () => tabs('music.active'), 5000).catch(() => false);
  check('clicking a card shows its tab, without the card', shown === true && cards('show') === 0,
    `${cards('show')} cards`);

  // Spread out (under the pointer), its close button pauses it and puts the card away
  // until it plays again.
  // Another tab of its window shown.
  await tabs(`chrome.tabs.update(tabs.find((t) => t.windowId === music.windowId
    && t.id !== music.id).id, { active: true }) ?? true`);
  await play();
  await sleep(1500);
  await snapshot(snapshots, `${shot}-close.png`, 'hover:MediaCard wait:400 click:CardButton:0');
  const closed = await until('paused', paused, 5000).catch(() => false);
  check("a card's close button pauses its music and puts the card away",
    closed && cards('close') === 0, `${cards('close')} cards`);
  await play();
  await sleep(1500);
  await snapshot(snapshots, `${shot}-again.png`);
  check('the card comes back when the music plays again', cards('again') === 1,
    `${cards('again')} cards`);
  for (const targetId of [music, other]) await o.send('Target.closeTarget', { targetId });
}

// Cookies and local storage a tab sees, as "cookie|localStorage".
const SEEN = `document.cookie + '|' + (localStorage.who ?? '')`;
const REMEMBER = (who) => `document.cookie = 'who=${who}; max-age=3600'; localStorage.who = '${who}'`;

async function containerChecks(o) {
  const page = await o.open('chrome://containers');
  const call = (...args) => o.containers(page, ...args);
  const state = await until('oblada://containers', () => call('getState').catch(() => null));
  const ids = state.containers.map((c) => c.id);
  check('oblada://containers lists the default containers', ['personal', 'work', 'banking', 'shopping'].every((id) => ids.includes(id)),
    state.containers.map((c) => c.name).join(', '));
  const cards = await until('container cards', () =>
    o.evaluate('document.querySelectorAll("#list .card").length', page)).catch(() => 0);
  check('oblada://containers page renders', cards === state.containers.length, `${cards} cards`);
  // A container's icon, picked among flat ones (as a workspace's is).
  await o.evaluate('document.querySelector("#list .icon-button").click()', page);
  const choices = await until('icon grid', () =>
    o.evaluate('document.querySelectorAll(".icon-grid button").length', page)).catch(() => 0);
  await o.evaluate(`document.querySelector(".icon-grid button[title='code']")?.click()`, page);
  const picked = await until('icon picked', async () =>
    (await call('getState')).containers.find((c) => c.id === 'personal')?.icon === 'code' || null, 5000)
    .catch(() => false);
  check('containers get a flat icon, picked among many', choices >= 64 && !!picked,
    `${choices} to pick from, ${picked ? 'picked' : 'not picked'}`);

  // The same site in two containers and in a normal tab.
  const tabFor = async (url) => o.attach((await until(url, async () =>
    (await o.pages()).find((p) => p.type === 'page' && p.url === url))).targetId);
  await call('openContainer', 'personal', `${site}?personal`);
  await call('openContainer', 'work', `${site}?work`);
  const tabs = {
    personal: await tabFor(`${site}?personal`),
    work: await tabFor(`${site}?work`),
    none: await o.open(`${site}?none`),
  };
  for (const [who, s] of Object.entries(tabs)) {
    // The page itself, not the blank document a new tab starts with.
    await until(`${who} page`, () =>
      o.evaluate('location.protocol === "http:" && document.readyState === "complete"', s));
    await o.evaluate(REMEMBER(who), s);
  }
  const seen = {};
  for (const [who, s] of Object.entries(tabs)) seen[who] = await o.evaluate(SEEN, s);
  check('containers keep separate cookies and storage',
    Object.entries(seen).every(([who, value]) => value === `who=${who}|${who}`), JSON.stringify(seen));

  // A link opened in a new tab (without an opener) stays in the container.
  await o.evaluate(`window.open('${site}?child', '_blank', 'noopener')`, tabs.work, true);
  const child = await tabFor(`${site}?child`);
  await until('child page', () => o.evaluate('document.readyState === "complete"', child));
  const childSeen = await o.evaluate(SEEN, child);
  check('links opened in a new tab stay in the container', childSeen === 'who=work|work', childSeen);

  // A new container tab shows the New Tab page.
  const before = new Set((await o.pages()).map((p) => p.targetId));
  await call('openContainer', 'banking', '');
  const ntp = await until('new container tab', async () => (await o.pages()).find((p) =>
    p.type === 'page' && !before.has(p.targetId) && p.url.startsWith('chrome://new')), 10000).catch(() => null);
  check('a new container tab opens the New Tab page', !!ntp, ntp?.url);

  // Clearing a container's data affects that container only.
  await call('clearContainer', 'personal');
  await o.send('Page.reload', {}, tabs.personal);
  await sleep(500);
  await until('reloaded page', () => o.evaluate('document.readyState === "complete"', tabs.personal));
  const afterClear = { personal: await o.evaluate(SEEN, tabs.personal), work: await o.evaluate(SEEN, tabs.work) };
  check('clearing a container erases its data only', afterClear.personal === '|' && afterClear.work === 'who=work|work',
    JSON.stringify(afterClear));

  // A site always opened in a container: a normal tab going there moves to it.
  const pageIn = (url, expected) => until(`${url} in its container`, async () => {
    const target = (await o.pages()).find((p) => p.type === 'page' && p.url === url);
    const s = target && await o.attach(target.targetId);
    return s && await o.evaluate(SEEN, s) === expected && s;
  }, 15000).catch(() => null);
  await call('assignSite', '127.0.0.1', 'work');
  const assigned = (await call('getState')).containers.find((c) => c.id === 'work').sites;
  await o.send('Target.createTarget', { url: `${site}?assigned` });
  check('a site always opened in a container goes there from a normal tab',
    !!await pageIn(`${site}?assigned`, 'who=work|work'), JSON.stringify(assigned));
  // ...unless the user picked another container for the tab.
  await call('openContainer', 'personal', `${site}?chosen`);
  check('opening it in another container on purpose keeps it there',
    !!await pageIn(`${site}?chosen`, '|'));
  await call('assignSite', '127.0.0.1', '');

  // Temporary containers: empty, and gone with their last tab.
  await call('openTemporary', `${site}?temporary`);
  const temporaryTab = await pageIn(`${site}?temporary`, '|');
  const temporary = (await call('getState')).containers.find((c) => c.temporary);
  if (temporaryTab) await o.evaluate(REMEMBER('temp'), temporaryTab);
  const temporarySeen = temporaryTab ? await o.evaluate(SEEN, temporaryTab) : null;
  check('a temporary container starts empty and keeps its own data',
    !!temporary && temporarySeen === 'who=temp|temp', `${temporary?.name}: ${temporarySeen}`);
  const temporaryTarget = (await o.pages()).find((p) => p.url === `${site}?temporary`);
  if (temporaryTarget) await o.send('Target.closeTarget', { targetId: temporaryTarget.targetId });
  const gone = await until('temporary container gone', async () =>
    !(await call('getState')).containers.some((c) => c.temporary) || null, 10000).catch(() => false);
  check('a temporary container goes away with its last tab', !!gone);

  // Workspaces: a new one shows its own tabs only, and its new tabs open in its
  // container (here "work", whose cookie a page there sees).
  const shot = path.join(tmp, 'workspace');
  // (Once it's shown: switching slides, as in Zen.)
  await snapshot(snapshots, `${shot}-new.png`, 'ws-new');
  await snapshot(snapshots, `${shot}.png`, 'ws-container:work');
  const layout = fs.readFileSync(`${shot}.layout.txt`, 'utf8');
  const shownTabs = layout.split('\n').filter((l) => /^\s*TabView \d+,\d+ \d+x\d+$/.test(l)).length;
  const buttons = layout.split('\n').filter((l) => /^\s*WorkspaceButton /.test(l)).length;
  check('a new workspace shows only its own tabs', shownTabs === 1 && buttons === 2,
    `${shownTabs} tabs, ${buttons} workspaces`);
  const known = new Set((await o.pages()).map((p) => p.targetId));
  await snapshot(snapshots, `${shot}-tab.png`, '34014');
  const newTab = await until('new tab in the workspace', async () => (await o.pages()).find((p) =>
    p.type === 'page' && !known.has(p.targetId)), 10000).catch(() => null);
  let workspaceSeen = null;
  if (newTab) {
    const s = await o.attach(newTab.targetId);
    await o.send('Page.navigate', { url: `${site}?workspace` }, s);
    await until('workspace page', () =>
      o.evaluate('location.protocol === "http:" && document.readyState === "complete"', s));
    workspaceSeen = await o.evaluate(SEEN, s);
  }
  check("a workspace's new tabs open in its container", workspaceSeen === 'who=work|work', workspaceSeen);
  await snapshot(snapshots, `${shot}-back.png`, 'ws-prev');

  // A two-finger swipe on the sidebar goes to the next workspace, and back
  // (macOS-like events, see oblada/browser/debug/window_snapshots.cc).
  const tabsShown = (file) => fs.readFileSync(file, 'utf8').split('\n')
    .filter((l) => /^\s*TabView \d+,\d+ \d+x\d+$/.test(l)).length;
  const home = tabsShown(`${shot}-back.layout.txt`);
  await snapshot(snapshots, `${shot}-swipe.png`, 'swipe:-150');
  const next = tabsShown(`${shot}-swipe.layout.txt`);
  await snapshot(snapshots, `${shot}-swipe-back.png`, 'swipe:150');
  const back = tabsShown(`${shot}-swipe-back.layout.txt`);
  check('swiping on the sidebar moves between workspaces', next === 2 && back === home,
    `${home} → ${next} → ${back} tabs`);

  // The workspace's name above its tabs is hidden, until Settings > Oblada turns it on
  // (through Settings' own API, which needs the pref allowed there).
  const headerShown = (file) => fs.readFileSync(file, 'utf8').split('\n')
    .some((l) => /^\s*TabGroupHeaderView \d+,\d+ \d+x\d+$/.test(l));
  const hiddenAtFirst = !headerShown(`${shot}-swipe-back.layout.txt`);
  const settingsId = (await o.send('Target.createTarget',
    { url: 'chrome://settings/oblada' })).targetId;
  const settings = await o.attach(settingsId);
  const toggle = `(function find(root) {
    const found = root.querySelector('settings-toggle-button[pref-key="oblada.workspaces.show_header"]');
    if (found) return found;
    for (const e of root.querySelectorAll('*')) {
      const inner = e.shadowRoot && find(e.shadowRoot);
      if (inner) return inner;
    }
    return null;
  })(document)`;
  const listed = await until('Settings > Oblada', () =>
    o.evaluate(`!!${toggle}?.checkVisibility()`, settings), 10000).catch(() => false);
  await o.evaluate(`${toggle}?.click()`, settings).catch(() => {});
  const turnedOn = await until('the header turned on', () => o.evaluate(`new Promise((r) =>
    chrome.settingsPrivate.getPref('oblada.workspaces.show_header', (p) => r(p?.value === true)))`,
  settings), 5000).catch(() => false);
  // The window's layout, picked there.
  const layouts = await o.evaluate(`[...document.querySelector('settings-ui').shadowRoot
    .querySelector('settings-main').shadowRoot.querySelectorAll(
      'settings-oblada-page controlled-radio-button')].map((b) => b.name).join(',')`,
  settings).catch(() => '');
  await o.send('Target.closeTarget', { targetId: settingsId });
  await snapshot(snapshots, `${shot}-header.png`, 'wait:300');
  check('Settings > Oblada offers the sidebar, scaffold and compact layouts',
    layouts === 'sidebar,scaffold,compact', layouts);
  check("the workspace's name shows above its tabs once Settings > Oblada turns it on",
    hiddenAtFirst && listed && turnedOn && headerShown(`${shot}-header.layout.txt`),
    `hidden at first: ${hiddenAtFirst}, in Settings: ${listed}, turned on: ${turnedOn}`);

  // A workspace's theme: the picker's buttons and colors ready to pick change it, and
  // the window takes it (its background, as the snapshot's layout says).
  const background = (file) => fs.readFileSync(file, 'utf8')
    .match(/^window background colors=(\S*) opacity=(\S+) grain=(\S+) glass=(\S+)$/m);
  const colors = (match) => (match?.[1] ? match[1].split(',').length : 0);
  await snapshot(snapshots, `${shot}-theme.png`, 'ws-theme-dots: wait:300 ws-theme');
  const opened = fs.existsSync(`${shot}-theme.popup.png`);
  const plain = background(`${shot}-theme.layout.txt`);
  await snapshot(snapshots, `${shot}-theme-add.png`, 'click:ThemeToolButton:3 click:ThemeToolButton:3');
  const added = background(`${shot}-theme-add.layout.txt`);
  check("the theme picker's + adds colors, which the window takes",
    opened && !!plain && colors(plain) === 0 && colors(added) === 2,
    `${opened ? 'open' : 'not open'}, ${colors(plain)} → ${colors(added)} colors`);
  await snapshot(snapshots, `${shot}-theme-preset.png`, 'click:ThemeSwatch:1');
  const preset = background(`${shot}-theme-preset.layout.txt`);
  check('a color ready to pick replaces them', colors(preset) === 1 && preset[1] !== added?.[1],
    preset?.[1]);
  await snapshot(snapshots, `${shot}-theme-glass.png`,
    'ws-theme-opacity:0.8 ws-theme-grain:0.5 ws-theme-scheme:light escape');
  const glass = background(`${shot}-theme-glass.layout.txt`);
  check("the window takes the theme's opacity, grain and scheme",
    glass?.[2] === '0.80' && glass?.[3] === '0.50' && glass?.[4] === 'light', glass?.[0]);
  await snapshot(snapshots, `${shot}-theme-back.png`,
    'ws-theme-opacity:default ws-theme-grain:0 ws-theme-scheme:auto');

  // The default theme: the workspaces without their own take it, and one that has
  // its own can take it back.
  await snapshot(snapshots, `${shot}-default.png`,
    'ws-theme-default default-theme-reset wait:300 default-theme');
  const reset = background(`${shot}-default.layout.txt`);
  await snapshot(snapshots, `${shot}-default-add.png`, 'click:ThemeToolButton:3');
  const followed = background(`${shot}-default-add.layout.txt`);
  check('a workspace without its own theme takes the default one',
    !!reset && colors(reset) === 0 && colors(followed) === 1,
    `${colors(reset)} → ${followed?.[1]}`);
  await snapshot(snapshots, `${shot}-own.png`, 'escape ws-theme-dots:120,0.6;300,0.6');
  const own = background(`${shot}-own.layout.txt`);
  await snapshot(snapshots, `${shot}-own-back.png`, 'ws-theme-default');
  const takenBack = background(`${shot}-own-back.layout.txt`);
  check("a workspace's own theme replaces the default one, until it takes it back",
    colors(own) === 2 && takenBack?.[1] === followed?.[1],
    `${own?.[1]} → ${takenBack?.[1]}`);
  await snapshot(snapshots, `${shot}-default-back.png`, 'default-theme-reset');

  const shown = (file, name) => fs.readFileSync(file, 'utf8')
    .match(new RegExp(`^\\s*${name} (-?\\d+),(-?\\d+) (\\d+)x(\\d+)(?!\\d| hidden)`, 'm'))
    ?.slice(1).map(Number);
  const controlsIn = (file) => fs.readFileSync(file, 'utf8')
    .match(/^window control (\d+),(\d+)/m)?.slice(1).map(Number);

  // Scaffold mode: the toolbar a row at the top of the window, the tabs' icons a
  // column below it, left of the page.
  await snapshot(snapshots, `${shot}-scaffold.png`, 'mode:scaffold wait:600');
  const scaffoldBar = shown(`${shot}-scaffold.layout.txt`, 'TopContainerView');
  const scaffoldTabs = shown(`${shot}-scaffold.layout.txt`, 'VerticalTabStripRegionView');
  const scaffoldPage = shown(`${shot}-scaffold.layout.txt`, 'MultiContentsView');
  check('scaffold, the address bar is a row at the top and the tabs icons on the side',
    scaffoldBar?.[0] === 0 && scaffoldBar?.[1] === 0 &&
      scaffoldBar?.[2] >= scaffoldPage?.[0] + scaffoldPage?.[2] && scaffoldTabs?.[2] < 60 &&
      scaffoldTabs?.[1] >= scaffoldBar?.[3] && scaffoldPage?.[0] > scaffoldTabs?.[0] + scaffoldTabs?.[2],
    `toolbar ${scaffoldBar}, tabs ${scaffoldTabs}, page ${scaffoldPage}`);

  // Compact mode (collapsing the sidebar, as with ⌘⇧L, goes back there): only the
  // page; the sidebar widens over it as a card with the window controls in its
  // corner.
  await snapshot(snapshots, `${shot}-compact.png`, 'mode:compact wait:600');
  const compactPage = shown(`${shot}-compact.layout.txt`, 'MultiContentsView');
  const compactTabs = shown(`${shot}-compact.layout.txt`, 'VerticalTabStripRegionView');
  const hiddenControls = controlsIn(`${shot}-compact.layout.txt`);
  await snapshot(snapshots, `${shot}-compact-open.png`, 'expand wait:600');
  const panel = shown(`${shot}-compact-open.layout.txt`, 'VerticalTabStripBackgroundBlurBackdrop');
  const controls = controlsIn(`${shot}-compact-open.layout.txt`);
  check('compact, only the page; the sidebar expands over it with the window controls',
    compactPage?.[0] === 8 && compactTabs?.[0] + compactTabs?.[2] <= 8 && !hiddenControls &&
      panel?.[2] > 200 && controls?.[0] > 13 && controls?.[1] > 13,
    `page ${compactPage}, tabs ${compactTabs}, panel ${panel}, window controls at ${controls}`);
  // Its address bar floats over the page too (here focused as with ⌘L).
  await snapshot(snapshots, `${shot}-compact-float.png`, 'expand wait:600 39001 wait:800');
  const floating = shown(`${shot}-compact-float.layout.txt`, 'LocationBarView');
  check('compact, the address bar floats over the page once focused',
    floating?.[0] > 100 && floating?.[2] > 300, `address bar at ${floating}`);
  await snapshot(snapshots, `${shot}-compact-back.png`, 'escape escape 34061 wait:600');
  const expanded = shown(`${shot}-compact-back.layout.txt`, 'VerticalTabStripRegionView');
  await snapshot(snapshots, `${shot}-compact-again.png`, '34061 wait:600');
  const again = shown(`${shot}-compact-again.layout.txt`, 'MultiContentsView');
  await snapshot(snapshots, `${shot}-sidebar.png`, 'mode:sidebar wait:600');
  check('⌘⇧L goes from compact to the sidebar and back to compact',
    expanded?.[2] > 200 && again?.[0] === 8, `sidebar ${expanded}, page then at ${again}`);
}

// The installed extensions as chrome://extensions-internals lists them.
const installedExtensions = async (o) => {
  const page = await o.open('chrome://extensions-internals');
  return until('extensions list', () =>
    o.evaluate('JSON.parse(document.querySelector("pre")?.textContent || "null")', page));
};

async function firefoxChecks(o) {
  // Downloading the .xpi installs it (the confirmation is auto-accepted in tests).
  await o.open(`${site}oblada-check-1.0.xpi`);
  const installed = await until('Firefox add-on installed', async () =>
    (await installedExtensions(o)).find((e) => e.name === 'Oblada Firefox check'), 20000).catch(() => null);
  check('a Firefox add-on (.xpi) installs', !!installed, installed ? installed.location : 'not installed');
  if (!installed) return;
  check('it is installed like any extension, not as an unpacked one', installed.location === 'INTERNAL',
    installed.location);

  // Its scripts use Firefox's names (browser.menus) and promise replies.
  const tab = await o.open(`${site}?firefox`);
  const reply = await until('add-on content script', () =>
    o.evaluate('document.documentElement.dataset.obladaFirefox', tab), 15000).catch(() => null);
  check('Firefox add-on scripts run (browser.menus, promise replies)',
    reply === JSON.stringify({ reply: 'oblada-pong', menus: 'function', theme: 'function' }), reply);

  // Firefox's containers API (contextualIdentities) and tabs' cookieStoreId.
  const containersTab = await o.open(`${site}?containers-test`);
  const result = await until('contextualIdentities test', () =>
    o.evaluate('document.documentElement.dataset.obladaContainers', containersTab), 20000).catch(() => null);
  const expected = JSON.stringify({
    created: true, listed: true, tab: true, query: 1, updated: 'Oblada CI 2', removed: 'Oblada CI 2', gone: true,
    events: ['created:Oblada CI', 'updated:Oblada CI 2', 'removed:Oblada CI 2'],
    stores: true, containerCookies: true, lenient: 'firefox-default',
  });
  check('Firefox add-ons manage containers (contextualIdentities, tabs cookieStoreId)', result === expected, result);

  // Once a new version is out where it says (its update_url), Oblada installs it.
  offerUpdate = true;
  const updated = await until('Firefox add-on updated', async () =>
    (await installedExtensions(o)).find((e) => e.name === 'Oblada Firefox check' && e.version === '1.1'),
    30000).catch(() => null);
  check('Firefox add-ons update themselves', !!updated, updated ? updated.version : 'still 1.0');
}

// The real thing: uBlock Origin from addons.mozilla.org, if the network is there.
async function amoChecks(o) {
  const store = await o.open('https://addons.mozilla.org/en-US/firefox/addon/ublock-origin/');
  const button = await until('AMO install button', () => o.evaluate(
    `document.querySelector('a.AMInstallButton-button, a[href$=".xpi"]')?.href`, store), 30000).catch(() => null);
  if (!button) {
    console.log('skip addons.mozilla.org checks (no network or the page changed)');
    return;
  }
  const userAgent = await o.evaluate('navigator.userAgent', store);
  check('addons.mozilla.org sees Firefox and offers its install button', /Firefox\//.test(userAgent), userAgent);
  await o.evaluate(`document.querySelector('a.AMInstallButton-button, a[href$=".xpi"]').click()`, store, true);
  const ublock = await until('uBlock Origin installed', async () =>
    (await installedExtensions(o)).find((e) => /uBlock Origin/.test(e.name)), 60000).catch(() => null);
  check('uBlock Origin installs from addons.mozilla.org', !!ublock, ublock?.version);
  if (!ublock) return;
  // On a real site (uBlock leaves local ones alone) opened after it started, an ad
  // request is blocked by the extension while the page itself loads. (Some ad
  // scripts aren't blocked but redirected to uBlock's harmless stand-ins.)
  const failures = new Map();
  o.onEvent(({ method, params, sessionId }) => {
    if (method === 'Network.loadingFailed') failures.set(sessionId, params.errorText);
  });
  let result;
  await until('ad request blocked', async () => {
    const page = await o.open('https://example.com/');
    await until('example.com', () => o.evaluate('document.readyState === "complete"', page), 15000);
    await o.send('Network.enable', {}, page);
    await o.evaluate(`fetch('https://adservice.google.com/adsid/integrator.js',
      { mode: 'no-cors', signal: AbortSignal.timeout(10000) }).catch(() => {})`, page);
    result = failures.get(page);
    return result === 'net::ERR_BLOCKED_BY_CLIENT';
  }, 90000).catch(() => {});
  check('uBlock Origin blocks ads', result === 'net::ERR_BLOCKED_BY_CLIENT', result ?? 'not blocked');

  // Firefox Multi-Account Containers, on Oblada's containers.
  const macStore = await o.open('https://addons.mozilla.org/en-US/firefox/addon/multi-account-containers/');
  await until('Multi-Account Containers install button', () => o.evaluate(
    `document.querySelector('a.AMInstallButton-button')?.href`, macStore), 30000).catch(() => null);
  await o.evaluate(`document.querySelector('a.AMInstallButton-button')?.click()`, macStore, true);
  const mac = await until('Multi-Account Containers installed', async () =>
    (await installedExtensions(o)).find((e) => /Multi-Account Containers/.test(e.name)), 60000).catch(() => null);
  check('Multi-Account Containers installs from addons.mozilla.org', !!mac, mac?.version);
  if (!mac) return;
  const background = await until('its background page', async () =>
    (await o.pages()).find((p) => p.type === 'background_page' && p.url.includes(mac.id)), 20000).catch(() => null);
  const session = background && await o.attach(background.targetId);
  const opened = session && await until('a tab opened in Work by it', () => o.evaluate(`(async () => {
    await backgroundLogic.openNewTab({ userContextId: '2', url: '${site}?mac', source: 'popup' });
    const tabs = await browser.tabs.query({ cookieStoreId: 'firefox-container-2', url: '${site}?mac' });
    return tabs.length > 0 && tabs[0].cookieStoreId;
  })()`, session), 20000).catch(() => null);
  const work = opened && await until('Work page', async () => {
    const target = (await o.pages()).find((p) => p.url === `${site}?mac`);
    return target && await o.evaluate(SEEN, await o.attach(target.targetId));
  }, 15000).catch(() => null);
  check('Multi-Account Containers opens tabs in Oblada\'s containers', work === 'who=work|work', `${opened} ${work}`);
}

// The tabs as oblada://discards (`page`) sees them: {id, url, loading, state}, with
// `loading` 0 for unloaded, 2 for loaded, and `state` 5 for discarded.
const tabStates = (o, page) => o.evaluate(`import('chrome://discards/discards.js')
  .then((m) => m.getOrCreateDetailsProvider().getTabDiscardsInfo())
  .then(({ infos }) => infos.map((i) => ({ id: i.id, url: i.tabUrl, loading: i.loadingState, state: i.state })))`, page);

async function restoreChecks(o) {
  // After a restart with the last session restored, the work tab is still in its container.
  const restored = await until('restored tab', async () =>
    (await o.pages()).find((p) => p.type === 'page' && p.url === `${site}?work`), 15000).catch(() => null);
  if (!restored) {
    check('container tabs are restored into their container', false,
      `not restored: ${(await o.pages()).filter((p) => p.type === 'page').map((p) => p.url).join(', ')}`);
    return;
  }
  // Restored tabs in the background wait to be opened before loading.
  const discards = await o.open('chrome://discards');
  await until('oblada://discards', () => tabStates(o, discards).catch(() => null));
  // (Time for Chromium to load the ones it would.)
  await sleep(3000);
  const sites = (await tabStates(o, discards)).filter((t) => t.url.startsWith(site));
  const unloaded = sites.filter((t) => t.loading === 0);
  check('restored tabs in the background load only once opened',
    unloaded.some((t) => t.url === `${site}?work`) && unloaded.length >= sites.length - 1,
    `${unloaded.length} of ${sites.length} not loaded`);

  await o.send('Target.activateTarget', { targetId: restored.targetId });
  const s = await o.attach(restored.targetId);
  await until('restored page', () => o.evaluate('document.readyState === "complete"', s), 30000);
  const value = await o.evaluate(SEEN, s);
  check('container tabs are restored into their container', value === 'who=work|work', value);

  // A container tab Chromium discards (to free memory, or for an extension's
  // tabs.discard()) reloads in its container.
  await o.send('Target.activateTarget', { targetId: (await o.pages()).find((p) => p.url.startsWith('chrome://discards')).targetId });
  const work = (await tabStates(o, discards)).find((t) => t.url === `${site}?work`);
  await o.evaluate(`import('chrome://discards/discards.js')
    .then((m) => m.getOrCreateDetailsProvider().discardById(${work.id}, 0))`, discards);
  // (Discarding gives it a new id.)
  const discarded = await until('the tab discarded', async () =>
    (await tabStates(o, discards)).find((t) => t.url === `${site}?work` && t.state === 5), 10000)
    .catch(() => null);
  const reopened = discarded && await until('the discarded tab', async () =>
    (await o.pages()).find((p) => p.type === 'page' && p.url === `${site}?work`), 10000).catch(() => null);
  let after = discarded ? 'not found after discarding' : 'not discarded';
  if (reopened) {
    await o.send('Target.activateTarget', { targetId: reopened.targetId });
    const reloaded = await o.attach(reopened.targetId);
    after = await until('the discarded tab reloaded', () => o.evaluate(
      'location.protocol === "http:" && document.readyState === "complete"', reloaded), 20000)
      .then(() => o.evaluate(SEEN, reloaded), (e) => e.message);
  }
  check('a discarded container tab reloads in its container', after === 'who=work|work', after);
}

async function closeChecks(o) {
  // The workspace shown keeps a New Tab page in place of its last tab (⌘W), and
  // closing that closes the window, whatever tabs the other workspaces have in it.
  const shot = path.join(tmp, 'close');
  const pages = async () => (await o.pages()).filter((p) => p.type === 'page');
  const isNewTab = (p) => /^chrome:\/\/(new-tab-page|newtab)\//.test(p.url);
  const known = new Set((await pages()).map((p) => p.targetId));
  await snapshot(snapshots, `${shot}-new.png`, 'ws-new');
  const tab = await until("the new workspace's tab", async () =>
    (await pages()).find((p) => !known.has(p.targetId)));
  await o.send('Page.navigate', { url: `${site}?last` }, await o.attach(tab.targetId));
  await until('its page', async () => (await pages()).some((p) => p.url === `${site}?last`));
  const before = await pages();
  await snapshot(snapshots, `${shot}-last.png`, '34015');
  const kept = await until('a New Tab page in its place', async () => {
    const now = await pages();
    return !now.some((p) => p.url === `${site}?last`) &&
      now.filter(isNewTab).length === before.filter(isNewTab).length + 1 && now;
  }, 5000).catch(() => null);
  check('closing the last tab of a workspace leaves a New Tab page', kept?.length === before.length,
    `${before.length} → ${kept ? kept.length : 'not kept'} tabs`);
  // (With the window gone, there's nothing to snapshot.)
  await snapshot(snapshots, `${shot}-new-tab.png`, '34015').catch(() => {});
  const closed = await until('the window closed', async () => (await pages()).length === 0, 10000)
    .then(() => true, () => false);
  check('closing that New Tab page closes the window', closed,
    closed ? 'closed' : `${(await pages()).length} tabs left`);
}

async function firstRunChecks() {
  // The first launch after installing opens Oblada's welcome, alone, and Chromium's
  // first run window (signing in to Google) is marked as done instead of opening.
  const firstRunProfile = path.join(tmp, 'first-run');
  const o = await launchOblada({ app, profile: firstRunProfile, firstRun: true });
  let tabs = [];
  let layouts = '';
  let picked = '';
  try {
    tabs = await until('first window', async () => {
      const pages = (await o.pages()).filter((p) => p.type === 'page');
      return pages.length && pages;
    });
    // Its layout step: the three layouts, and picking one sets the window's.
    const welcome = await o.attach(tabs[0].targetId);
    layouts = await until('the layouts', () => o.evaluate(
      `[...document.querySelectorAll('.layout')].map((l) => l.dataset.mode).join(',')`,
      welcome), 10000).catch(() => '');
    // Clicked until it takes: the layouts show before the page's script (welcome.js, a
    // module) listens to them, which on a slow first launch is a while.
    picked = await until('the layout picked', async () => {
      await o.evaluate(`document.querySelector('.layout[data-mode="scaffold"]').click()`, welcome);
      const mode = await o.evaluate(
        `import('chrome://resources/js/cr.js').then((cr) => cr.sendWithPromise('getLayout'))`, welcome);
      return mode === 'scaffold' && mode;
    }, 5000).catch(() => '');
  } finally {
    await o.kill();
  }
  check('the welcome offers the three layouts, and picking one sets it',
    layouts === 'sidebar,scaffold,compact' && picked === 'scaffold', `${layouts}; picked ${picked}`);
  const state = JSON.parse(fs.readFileSync(path.join(firstRunProfile, 'Local State'), 'utf8'));
  check('the first launch opens the welcome, not Chromium\'s first run',
    tabs.length === 1 && tabs[0].url === 'chrome://welcome/' && state.browser?.first_run_finished === true,
    `${tabs.map((t) => t.url).join(', ')}; first run finished: ${state.browser?.first_run_finished}`);
}

// The user keeps working meanwhile, so the check is that Oblada is never the frontmost app.
const focus = watchFocus();
let oblada;
try {
  await firstRunChecks();
  oblada = await launch();
  await coreChecks(oblada);
  await splitChecks(oblada);
  await peekChecks(oblada);
  await cleanLinkChecks(oblada);
  await mediaChecks(oblada);
  await containerChecks(oblada);
  await firefoxChecks(oblada);
  if (process.env.OBLADA_SMOKE_NETWORK !== '0') await amoChecks(oblada);
  await oblada.quit();
  // No flag asking for it: Oblada restores the last session by default.
  oblada = await launch();
  await restoreChecks(oblada);
  await closeChecks(oblada);
} catch (e) {
  check('smoke test ran', false, e.message);
  const stderr = oblada?.stderr() ?? '';
  if (stderr) console.log(stderr.split('\n').filter((l) => !/cv_display_link|gcm\/engine/.test(l)).slice(-15).join('\n'));
} finally {
  await oblada?.kill();
  const { names, tookFocus } = focus.stop();
  check('never took focus', !tookFocus, names.join(', '));
  server.close();
  fs.rmSync(tmp, { recursive: true, force: true });
}
process.exit(results.every(Boolean) ? 0 : 1);
