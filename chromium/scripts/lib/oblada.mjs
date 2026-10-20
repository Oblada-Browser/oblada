// Runs the Oblada build headless and drives it over the DevTools protocol. Headless Oblada
// never shows a window or takes focus, so checks can run while the user works.
import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const src = path.join(process.env.OBLADA_CHROMIUM ?? path.join(os.homedir(), 'chromium'), 'src');
// In out/Oblada, else in the checkout's existing build directory (see env.sh).
const outDirs = fs.existsSync(path.join(src, 'out'))
  ? fs.readdirSync(path.join(src, 'out')).map((dir) => path.join(src, 'out', dir)) : [];
export const defaultApp = [path.join(src, 'out/Oblada'), ...outDirs]
  .map((dir) => path.join(dir, 'Oblada.app'))
  .find((app) => fs.existsSync(app)) ?? path.join(src, 'out/Oblada/Oblada.app');

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Retries `fn` until it returns something truthy; errors count as "not yet" (a page
// that is still loading can't be evaluated).
export const until = async (what, fn, ms = 20000) => {
  let error;
  for (const end = Date.now() + ms; Date.now() < end; await sleep(200)) {
    try {
      const value = await fn();
      if (value) return value;
    } catch (e) {
      error = e;
    }
  }
  throw new Error(`timed out waiting for ${what}${error ? ` (${error.message})` : ''}`);
};

// Name and pid of the frontmost app.
const frontApp = () => {
  try {
    const asn = execFileSync('lsappinfo', ['front']).toString().trim();
    const info = execFileSync('lsappinfo', ['info', '-only', 'name', '-only', 'pid', asn]).toString();
    return { name: info.match(/^"([^"]*)"/)?.[1], pid: Number(info.match(/\bpid = (\d+)/)?.[1]) };
  } catch {
    return {};
  }
};

// Pids of the Oblada processes this script started.
const launched = new Set();

// Watches the frontmost app until stop(), which returns the names seen and whether one
// of the Obladas started here ever came to the front (the user's own Oblada may, of course).
export function watchFocus() {
  const names = new Set();
  let tookFocus = false;
  const timer = setInterval(() => {
    const { name, pid } = frontApp();
    if (name) names.add(name);
    if (launched.has(pid) && !tookFocus) {
      tookFocus = true;
      console.log(`[${new Date().toISOString()}] WARNING: Oblada (pid ${pid}) came to the front`);
    }
  }, 250);
  return { stop: () => { clearInterval(timer); return { names: [...names], tookFocus }; } };
}

// Starts Oblada on `profile` and connects to it over the DevTools protocol.
// A copy of `app` that macOS takes for another app (its own bundle id), made again
// after each build: clicking Oblada in the Dock while a check runs then opens the
// user's Oblada, instead of bringing the headless one to the front. It's an APFS
// clone, so instant and taking no space. Falls back to `app` itself.
function testCopy(app) {
  const copy = path.join(path.dirname(app), 'Oblada Test.app');
  const framework = (a) =>
    path.join(a, 'Contents/Frameworks/Oblada Framework.framework/Versions/Current/Oblada Framework');
  const stampFile = path.join(path.dirname(app), '.oblada-test-build');  // what it's a copy of
  try {
    const stamp = String(fs.statSync(framework(app)).mtimeMs);
    if (fs.existsSync(stampFile) && fs.readFileSync(stampFile, 'utf8') === stamp) return copy;
    fs.rmSync(copy, { recursive: true, force: true });
    fs.rmSync(stampFile, { force: true });
    execFileSync('cp', ['-c', '-R', app, copy]);
    const plist = path.join(copy, 'Contents/Info.plist');
    const id = execFileSync('/usr/libexec/PlistBuddy', ['-c', 'Print :CFBundleIdentifier', plist])
      .toString().trim();
    execFileSync('/usr/libexec/PlistBuddy', ['-c', `Set :CFBundleIdentifier ${id}.test`, plist]);
    execFileSync('codesign', ['--force', '--sign', '-', copy], { stdio: 'ignore' });
    fs.writeFileSync(stampFile, stamp);
    return copy;
  } catch (e) {
    console.log(`(no test copy of ${app}: ${e.message.split('\n')[0]})`);
    fs.rmSync(copy, { recursive: true, force: true });
    return app;
  }
}

// `firstRun`: as the first launch after installing, rather than skipping what that
// opens (headless Oblada always passes --no-first-run, which this overrides).
export async function launch({ app = defaultApp, profile, args = [], firstRun = false }) {
  const portFile = path.join(profile, 'DevToolsActivePort');
  fs.mkdirSync(profile, { recursive: true });
  fs.rmSync(portFile, { force: true });
  const proc = spawn(path.join(testCopy(app), 'Contents/MacOS/Oblada'), [
    '--headless',
    `--user-data-dir=${profile}`,
    '--remote-debugging-port=0',
    firstRun ? '--force-first-run' : '--no-first-run',
    '--no-default-browser-check',
    // Never the user's keychain (a new build would have to be allowed to it first).
    '--use-mock-keychain',
    ...args,
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  launched.add(proc.pid);
  let stderr = '';
  proc.stderr.on('data', (d) => {
    stderr += d;
    // OBLADA_LOG=1: the browser's log too.
    if (process.env.OBLADA_LOG) process.stderr.write(d);
  });
  const exited = new Promise((r) => proc.once('exit', r));

  // The first launch of a new build is slow: macOS checks the whole app first.
  let devTools;
  try {
    devTools = await until('DevTools', () =>
      fs.existsSync(portFile) && fs.readFileSync(portFile, 'utf8').trim().split('\n'),
    60000);
  } catch (error) {
    proc.kill();
    throw error;
  }
  const [port, wsPath] = devTools;
  const ws = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 0;
  const pending = new Map();
  const listeners = new Set();
  ws.onmessage = ({ data }) => {
    const msg = JSON.parse(data);
    if (msg.method) {
      for (const listener of listeners) listener(msg);
      return;
    }
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result);
  };
  ws.onclose = () => {
    for (const p of pending.values()) p.reject(new Error('Oblada closed the connection'));
  };
  // Every call gets an answer within `timeout`, so a stuck page can't hang a check.
  const send = (method, params = {}, sessionId, timeout = 30000) => new Promise((resolve, reject) => {
    const id = ++nextId;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`${method} timed out`));
    }, timeout);
    pending.set(id, {
      resolve: (v) => { clearTimeout(timer); resolve(v); },
      reject: (e) => { clearTimeout(timer); reject(e); },
    });
    ws.send(JSON.stringify({ id, method, params, sessionId }));
  });
  const o = {
    send,
    // Calls `listener` with every DevTools event ({method, params, sessionId}).
    onEvent: (listener) => listeners.add(listener),
    stderr: () => stderr,
    pages: async () => (await send('Target.getTargets')).targetInfos,
    attach: async (targetId) =>
      (await send('Target.attachToTarget', { targetId, flatten: true })).sessionId,
    open: async (url) => o.attach((await send('Target.createTarget', { url })).targetId),
    evaluate: async (expression, session, userGesture = false) => {
      const { result, exceptionDetails } = await send('Runtime.evaluate',
        { expression, returnByValue: true, awaitPromise: true, userGesture }, session);
      if (exceptionDetails) throw new Error(exceptionDetails.exception?.description ?? exceptionDetails.text);
      return result.value;
    },
    // Visible text of a page, including the shadow roots WebUI pages are built from.
    text: (session) => o.evaluate(`(function walk(n) {
      if (/^(SCRIPT|STYLE|TEMPLATE)$/.test(n.nodeName)) return '';
      let s = n.shadowRoot ? walk(n.shadowRoot) : '';
      for (const c of n.childNodes) s += c.nodeType === 3 ? c.textContent : walk(c);
      return s;
    })(document)`, session),
    // Calls a chrome://containers handler (`page` is a session on that page).
    containers: (page, ...args) => o.evaluate(
      `import('chrome://resources/js/cr.js').then((cr) => cr.sendWithPromise(${args.map((a) => JSON.stringify(a)).join(', ')}))`,
      page),
    // Quits like the user would, so the session is saved.
    quit: async () => {
      await send('Browser.close').catch(() => {});
      await Promise.race([exited, sleep(10000)]);
      ws.close();
    },
    kill: async () => {
      ws.close();
      proc.kill();
      await Promise.race([exited, sleep(5000)]);
    },
  };
  return o;
}

// Writes the browser window, UI included, to `file` (needs --oblada-snapshot-dir=`dir`).
export async function snapshot(dir, file, actions = '') {
  // `actions`: browser command ids, "click:<view class>", "split", "drag:<n>:<x>,<y>"
  // and "drop", done before (see oblada/browser/debug/window_snapshots.cc).
  const png = path.join(dir, 'window.png');
  const popup = path.join(dir, 'popup.png');
  const mtime = (f) => (fs.existsSync(f) ? fs.statSync(f).mtimeMs : 0);
  const before = mtime(png);
  const popupBefore = mtime(popup);
  fs.writeFileSync(path.join(dir, 'request'), actions);
  await until('window snapshot', () => mtime(png) > before, 10000);
  fs.copyFileSync(png, file);
  const base = file.replace(/\.png$/, '');
  // The window's views and where they are, next to the image as <name>.layout.txt.
  const layout = path.join(dir, 'layout.txt');
  if (fs.existsSync(layout)) fs.copyFileSync(layout, base + '.layout.txt');
  // A bubble open on the window, as <name>.popup.png.
  if (mtime(popup) > popupBefore) fs.copyFileSync(popup, base + '.popup.png');
}
