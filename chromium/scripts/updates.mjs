#!/usr/bin/env node
// Checks that Oblada updates itself (oblada/browser/updater), end to end: packs the build
// as a release (dmg.sh, appcast.py) with a key made for the test, serves the feed and the
// update archive from here, and runs an older copy of the app (out of the DMG), which must download the update and
// install it when it quits (or right away, from the About page's Relaunch button), and
// refuse what isn't signed with the key. Everything runs headless, nothing leaves this
// machine, and the apps are copies in a temporary folder with a bundle id of their own
// (Sparkle's settings and caches go by it), removed at the end.
//
//   node chromium/scripts/updates.mjs [path/to/Oblada.app]
import { execFileSync, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defaultApp, launch, sleep, until, watchFocus } from './lib/oblada.mjs';

const build = process.argv[2] ?? defaultApp;
const scripts = path.dirname(fileURLToPath(import.meta.url));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'oblada-updates-'));
const read = (file) => fs.readFileSync(path.join(scripts, file), 'utf8').trim();
// The update is the version being built; the installed app pretends to be older.
const NEW = read('../oblada/VERSION');
const OLD = '0.0.1';
const plist = (app) => path.join(app, 'Contents/Info.plist');
const plistGet = (app, key) =>
  execFileSync('/usr/libexec/PlistBuddy', ['-c', `Print :${key}`, plist(app)]).toString().trim();
const plistSet = (app, key, value) =>
  execFileSync('/usr/libexec/PlistBuddy', ['-c', `Set :${key} ${value}`, plist(app)]);
const bundleId = `${plistGet(build, 'CFBundleIdentifier')}.update-test`;

const results = [];
const check = (name, ok, detail = '') => {
  results.push(ok);
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `  (${detail})` : ''}`);
};

// Keys as Sparkle's tools take them: the private half is the base64 of its 32-byte seed.
function makeKey(name) {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ed25519');
  const file = path.join(tmp, `${name}.key`);
  fs.writeFileSync(file, privateKey.export({ format: 'der', type: 'pkcs8' }).subarray(-32).toString('base64'));
  return { file, public: publicKey.export({ format: 'der', type: 'spki' }).subarray(-32).toString('base64') };
}
const key = makeKey('test');
const otherKey = makeKey('other');

// The feed and the update archive, as GitHub would serve them; `routes` changes with each
// case.
let routes = {};
let requests = [];
const server = http.createServer((req, res) => {
  requests.push(req.url);
  const file = routes[req.url];
  if (!file) {
    res.statusCode = 404;
    res.end();
    return;
  }
  res.setHeader('Content-Type', { '.xml': 'application/xml', '.html': 'text/html' }[path.extname(req.url)] ??
    'application/octet-stream');
  res.setHeader('Content-Length', fs.statSync(file).size);
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const site = `http://127.0.0.1:${server.address().port}`;
const page = `${site}/page.html`;
routes['/page.html'] = path.join(tmp, 'page.html');
fs.writeFileSync(routes['/page.html'], '<!doctype html><title>Oblada update test page</title>');

// Sparkle's own processes for the test's apps (its installer and Updater.app, from the
// apps' Sparkle.framework, and what they run on its caches): only those, never anything
// else.
const sparkleCaches = path.join(os.homedir(), 'Library/Caches', bundleId);
const sparkleProcesses = () => {
  const out = spawnSync('pgrep', ['-f', `${tmp}/.*Sparkle\\.framework|${sparkleCaches}`]).stdout.toString().trim();
  return out ? out.split('\n').map(Number) : [];
};
const running = (app) => spawnSync('pgrep', ['-f', path.join(app, 'Contents/MacOS/Oblada')]).status === 0;

// Forgets what Sparkle remembers (when it last checked, what it downloaded).
function resetSparkle({ checkedNow = false } = {}) {
  spawnSync('defaults', ['delete', bundleId], { stdio: 'ignore' });
  fs.rmSync(sparkleCaches, { recursive: true, force: true });
  if (checkedNow) {
    // As if it had just checked: no check at startup, the About page's is the first.
    const now = new Date().toISOString().replace('T', ' ').replace(/\.\d+Z$/, ' +0000');
    execFileSync('defaults', ['write', bundleId, 'SULastCheckTime', '-date', now]);
  }
}

// Sparkle's log since `since`, when a check fails.
const sparkleLog = (since) => spawnSync('log', ['show', '--style', 'compact', '--start', since,
  '--predicate', 'subsystem == "org.sparkle-project.Sparkle" OR process == "Autoupdate" OR process == "Updater"'])
  .stdout.toString().split('\n').slice(-25).join('\n');
const logTime = () => new Date().toISOString().replace('T', ' ').replace(/\.\d+Z$/, '');

// Opens the About page and waits for its update status (what the updater reports to it,
// UpdateStatusChangedEvent) to be one of `statuses`; returns it with every status shown.
async function waitForStatus(o, statuses, ms = 120000) {
  const session = await o.open('chrome://settings/help');
  await until('About page', () => o.evaluate('document.readyState === "complete" && !!window.chrome?.send', session));
  await o.evaluate(`import('chrome://resources/js/cr.js').then((cr) => {
    window.obladaStatuses = [];
    cr.addWebUiListener('update-status-changed',
      (e) => window.obladaStatuses.push({ status: e.status, message: e.message }));
    chrome.send('refreshUpdateStatus');
  })`, session);
  const shown = async () => {
    const list = await o.evaluate('window.obladaStatuses', session);
    return { last: list.at(-1), seen: list.map((s) => s.status).filter((s, i, a) => s !== a[i - 1]) };
  };
  return until(`About page: ${statuses.join(' or ')}`, async () => {
    const { last, seen } = await shown();
    return statuses.includes(last?.status) && { session, ...last, seen };
  }, ms).catch(async () => {
    const { seen } = await shown().catch(() => ({ seen: [] }));
    return { session, status: 'timeout', seen: seen.length ? seen : ['nothing'] };
  });
}

// --- The release: the build packed as dmg.sh packs it, and its feed.

console.log(`packing Oblada ${NEW} as a release, with a test key...`);
const source = path.join(tmp, 'source/Oblada.app');
fs.mkdirSync(path.dirname(source));
execFileSync('cp', ['-c', '-R', build, source]);
plistSet(source, 'CFBundleIdentifier', bundleId);
const release = path.join(tmp, 'release');
// What updates download, and the DMG.
const [archive, dmg] = execFileSync(path.join(scripts, 'dmg.sh'), ['--app', source, '--version', NEW,
  '--feed', `${site}/appcast.xml`, '--public-key', key.public, '--out', release],
{ stdio: ['ignore', 'pipe', 'inherit'] }).toString().trim().split('\n').slice(-2);
fs.rmSync(path.dirname(source), { recursive: true });
const archivePath = `/${path.basename(archive)}`;
const appcast = (name, archiveKey = key, extra = []) => {
  const out = path.join(release, name);
  execFileSync('python3', [path.join(scripts, 'appcast.py'), '--archive', archive, '--url', `${site}${archivePath}`,
    '--version', NEW, '--out', out, '--key-file', archiveKey.file,
    '--changelog', path.join(scripts, `../../changelogs/${NEW.replaceAll('.', '_')}.json`), ...extra]);
  return out;
};
const feed = appcast('appcast.xml');
const feedText = fs.readFileSync(feed, 'utf8');
check('the feed names the update and signs it and itself',
  feedText.includes(`<sparkle:version>${NEW}</sparkle:version>`) &&
  /sparkle:edSignature="[^"]+" length="\d+"/.test(feedText) && /sparkle-signatures:/.test(feedText),
  path.basename(archive));

// The app as users get it, out of the DMG.
const pristine = path.join(tmp, 'pristine/Oblada.app');
fs.mkdirSync(path.join(tmp, 'mnt'));
execFileSync('hdiutil', ['attach', '-quiet', '-nobrowse', '-readonly', '-noautoopen', '-mountpoint', path.join(tmp, 'mnt'), dmg]);
execFileSync('ditto', [path.join(tmp, 'mnt/Oblada.app'), pristine]);
execFileSync('hdiutil', ['detach', '-quiet', path.join(tmp, 'mnt')]);
check('the DMG\'s app has the feed, the key and its version',
  plistGet(pristine, 'SUFeedURL') === `${site}/appcast.xml` && plistGet(pristine, 'SUPublicEDKey') === key.public &&
  plistGet(pristine, 'CFBundleVersion') === NEW &&
  fs.existsSync(path.join(pristine, 'Contents/Frameworks/Sparkle.framework')));

// An installed copy pretending to be `version`, re-signed as dmg.sh signs.
let installs = 0;
function install(version) {
  const app = path.join(tmp, `install-${++installs}/Oblada.app`);
  fs.mkdirSync(path.dirname(app));
  execFileSync('cp', ['-c', '-R', pristine, app]);
  plistSet(app, 'CFBundleVersion', version);
  execFileSync('codesign', ['--force', '--sign', '-', app], { stdio: 'ignore' });
  return app;
}

// A new profile, whose downloads (if any) go to the test's folder, not the user's.
let profiles = 0;
function newProfile() {
  const profile = path.join(tmp, `profile-${++profiles}`);
  fs.mkdirSync(path.join(profile, 'Default'), { recursive: true });
  fs.writeFileSync(path.join(profile, 'Default/Preferences'), JSON.stringify({
    download: { default_directory: tmp, prompt_for_download: false },
    savefile: { default_directory: tmp },
  }));
  return profile;
}

const waitForVersion = (app, version, ms = 180000) =>
  until(`${app} at ${version}`, () => plistGet(app, 'CFBundleVersion') === version, ms).then(() => true, () => false);

// After a case: nothing of Sparkle's left running.
async function settle(what) {
  const left = await until('Sparkle to finish', () => sparkleProcesses().length === 0 || null, 30000)
    .then(() => [], () => sparkleProcesses());
  if (left.length) {
    check(`${what}: Sparkle's processes ended`, false, `still running: ${left.join(', ')}`);
    for (const pid of left) process.kill(pid);
  }
}

// --- The cases.

// A rejected update: the app shows an error and stays as it is after quitting.
async function rejects(name, { version = OLD, expect = ['failed'], archiveRequested } = {}) {
  resetSparkle();
  requests = [];
  const app = install(version);
  const since = logTime();
  const o = await launch({ app, profile: newProfile(), copy: false });
  const status = await waitForStatus(o, expect.concat(expect.includes('failed') ? [] : ['failed', 'nearly_updated']));
  await o.quit();
  await sleep(10000);
  const unchanged = plistGet(app, 'CFBundleVersion') === version;
  const downloaded = requests.includes(archivePath);
  const ok = expect.includes(status.status) && unchanged &&
    (archiveRequested === undefined || downloaded === archiveRequested);
  check(name, ok, `About: ${status.seen.join(' > ')}${status.message ? `: ${status.message}` : ''}; ` +
    `version ${plistGet(app, 'CFBundleVersion')}; ${downloaded ? '' : 'not '}downloaded`);
  if (!ok) console.log(sparkleLog(since));
  await settle(name);
}

const focus = watchFocus();
try {
  // A build doesn't update itself: Sparkle is in it, but not the feed nor the key.
  {
    const o = await launch({ app: build, profile: newProfile() });
    const status = await waitForStatus(o, ['disabled'], 30000);
    await o.quit();
    check('a build doesn\'t update itself (no update status on the About page)',
      status.status === 'disabled' && fs.existsSync(path.join(build, 'Contents/Frameworks/Sparkle.framework')),
      status.seen.join(' > '));
  }

  // The main road: checked at startup, downloaded, installed when Oblada quits.
  {
    routes = { ...routes, '/appcast.xml': feed, [archivePath]: archive };
    resetSparkle();
    requests = [];
    const app = install(OLD);
    const since = logTime();
    const o = await launch({ app, profile: newProfile(), copy: false });
    const status = await waitForStatus(o, ['nearly_updated', 'failed']);
    check('it finds the update at startup and downloads it', status.status === 'nearly_updated' &&
      requests.includes('/appcast.xml') && requests.includes(archivePath),
      `About: ${status.seen.join(' > ')}${status.message ? `: ${status.message}` : ''}`);
    if (status.status !== 'nearly_updated') console.log(sparkleLog(since));
    check('the app isn\'t replaced while it runs', plistGet(app, 'CFBundleVersion') === OLD);
    await o.quit();
    const installed = await waitForVersion(app, NEW);
    check('quitting installs it', installed, `version ${plistGet(app, 'CFBundleVersion')}`);
    if (!installed) console.log(sparkleLog(since));
    const verify = spawnSync('codesign', ['--verify', '--deep', '--strict', app]);
    check('the installed app\'s signature holds', verify.status === 0, verify.stderr.toString().trim());
    const xattrs = spawnSync('xattr', ['-lr', app]).stdout.toString();
    check('it isn\'t quarantined (no Gatekeeper prompt after an update)', !xattrs.includes('com.apple.quarantine'));
    await sleep(3000);
    check('a headless Oblada isn\'t opened again as a window', !running(app));
    await settle('install on quit');

    // The new version runs, and finds itself up to date.
    resetSparkle();
    const updated = await launch({ app, profile: newProfile(), copy: false });
    const again = await waitForStatus(updated, ['updated', 'nearly_updated', 'failed']);
    await updated.quit();
    check('the updated app runs and is up to date', again.status === 'updated', again.seen.join(' > '));
    await settle('updated app');
  }

  // The About page's check, then its Relaunch button: installed right away, the session
  // restored on the next start even though startup opens the New Tab page.
  {
    resetSparkle({ checkedNow: true });
    requests = [];
    const app = install(OLD);
    const since = logTime();
    const profile = newProfile();
    const o = await launch({ app, profile, copy: false });
    // "On startup: open the New Tab page", set as Settings sets it (Chromium resets the
    // pref when its file is edited).
    const settings = await o.open('chrome://settings/onStartup');
    await until('Settings', () => o.evaluate('!!chrome.settingsPrivate', settings));
    await o.evaluate(`new Promise((r) => chrome.settingsPrivate.setPref('session.restore_on_startup', 5, '', r))`, settings);
    await o.open(page);
    await sleep(1000);
    check('no check at startup a day after the last', !requests.includes('/appcast.xml'));
    const status = await waitForStatus(o, ['nearly_updated', 'failed']);
    check('the About page checks, downloads and offers to relaunch', status.status === 'nearly_updated' &&
      requests.includes('/appcast.xml') && requests.includes(archivePath), status.seen.join(' > '));
    // The page's own button, in its shadow root.
    await o.evaluate(`(() => {
      const find = (root) => {
        for (const el of root.querySelectorAll('*')) {
          if (el.localName === 'settings-about-page') return el;
          const found = el.shadowRoot && find(el.shadowRoot);
          if (found) return found;
        }
      };
      find(document).shadowRoot.querySelector('#relaunch').click();
    })()`, status.session, true).catch(() => {});
    const quit = await Promise.race([o.exited.then(() => true), sleep(60000).then(() => false)]);
    check('Relaunch quits Oblada to install the update', quit);
    const restoreFlag = JSON.parse(fs.readFileSync(path.join(profile, 'Local State'), 'utf8'))
      .oblada?.updater?.restore_session;
    const exitType = JSON.parse(fs.readFileSync(path.join(profile, 'Default/Preferences'), 'utf8'))
      .profile?.exit_type;
    const installed = await waitForVersion(app, NEW);
    check('and installs it', installed, `version ${plistGet(app, 'CFBundleVersion')}`);
    if (!quit || !installed) console.log(sparkleLog(since));
    await sleep(3000);
    check('a headless Oblada isn\'t relaunched as a window', !running(app));
    await o.kill();
    await settle('relaunch');

    const updated = await launch({ app, profile, copy: false });
    const restored = await until('restored tabs', async () =>
      (await updated.pages()).some((p) => p.url === page), 20000).catch(() => false);
    const tabs = (await updated.pages()).filter((p) => p.type === 'page').map((p) => p.url);
    await updated.quit();
    const state = JSON.parse(fs.readFileSync(path.join(profile, 'Local State'), 'utf8'));
    check('the next start restores the session, though startup opens the New Tab page',
      restoreFlag === true && restored && !state.oblada?.updater?.restore_session,
      `quit ${exitType}, restore flag ${restoreFlag} then ${state.oblada?.updater?.restore_session}; ` +
      `tabs: ${tabs.join(', ')}`);
    await settle('restored session');
  }

  // What must be refused.
  routes['/appcast.xml'] = feed;
  routes[archivePath] = archive;
  await rejects('it doesn\'t downgrade', { version: '9.9.9', expect: ['updated'], archiveRequested: false });

  const otherKeyFeed = appcast('appcast-other-key.xml', otherKey);
  // The archive's signature by another key, in a feed signed by the right one.
  const resigned = path.join(release, 'appcast-resigned.xml');
  fs.writeFileSync(resigned, fs.readFileSync(otherKeyFeed, 'utf8').replace(/\n?<!-- sparkle-signatures:[\s\S]*?-->\n?/, '\n'));
  execFileSync(path.join(process.env.OBLADA_CHROMIUM ?? path.join(os.homedir(), 'chromium'),
    'src/third_party/sparkle/bin/sign_update'), [resigned, '--ed-key-file', key.file]);
  routes['/appcast.xml'] = resigned;
  await rejects('it refuses an update signed with another key');

  const tampered = path.join(release, 'tampered.aar');
  fs.copyFileSync(archive, tampered);
  const bytes = fs.readFileSync(tampered);
  bytes[Math.floor(bytes.length / 2)] ^= 0xff;
  fs.writeFileSync(tampered, bytes);
  routes['/appcast.xml'] = feed;
  routes[archivePath] = tampered;
  await rejects('it refuses an update changed after it was signed');

  routes[archivePath] = archive;
  const forged = path.join(release, 'appcast-forged.xml');
  fs.writeFileSync(forged, feedText.replace(/<title>Oblada [^<]*<\/title>/, '<title>Oblada forged</title>'));
  routes['/appcast.xml'] = forged;
  await rejects('it refuses a feed changed after it was signed', { archiveRequested: false });

  routes['/appcast.xml'] = otherKeyFeed;
  await rejects('it refuses a feed signed with another key', { archiveRequested: false });

  delete routes['/appcast.xml'];
  await rejects('a feed that isn\'t there is an error, nothing more', { archiveRequested: false });
} catch (e) {
  check('update test ran', false, e.stack);
} finally {
  const { names, tookFocus } = focus.stop();
  check('never took focus', !tookFocus && !names.includes('Updater'), names.join(', '));
  for (const pid of sparkleProcesses()) process.kill(pid);
  server.close();
  resetSparkle();
  fs.rmSync(path.join(os.homedir(), 'Library/Preferences', `${bundleId}.plist`), { force: true });
  fs.rmSync(path.join(os.homedir(), 'Library/HTTPStorages', bundleId), { recursive: true, force: true });
  fs.rmSync(tmp, { recursive: true, force: true });
}
process.exit(results.every(Boolean) ? 0 : 1);
