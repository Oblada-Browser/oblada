#!/usr/bin/env node
// Takes a screenshot of Oblada's window, browser UI included, without ever showing it:
// headless Oblada with a throwaway profile, a few tabs open (some in containers).
//
//   node chromium/scripts/screenshot.mjs [out.png] [--width=1400] [--height=900]
//       [--mode=scaffold|compact] [--sidebar-width=280] [--media=2] [--do="<command ids, click:<view class>>"]
//
// --mode sets the window's layout (--compact is --mode=compact); --sidebar-width sets the width the
// sidebar was left at; --media opens that many tabs playing (silent) music, behind the
// others, for the media controls at the bottom of the sidebar; --do runs browser commands (e.g. 39001, focus the address bar),
// clicks views, "split"s the active tab or "drag:<n>:<x>,<y>"s a tab over the page
// before the screenshot (see oblada/browser/debug/window_snapshots.cc); "film:<count>:<ms>"
// also saves frames of what they do, <ms> apart, as <out>.film-<i>.png.
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { launch, silentWav, sleep, snapshot, until, watchFocus } from './lib/oblada.mjs';

const args = process.argv.slice(2);
const out = path.resolve(args.find((a) => !a.startsWith('--')) ?? 'oblada.png');
const option = (name, fallback) =>
  Number(args.find((a) => a.startsWith(`--${name}=`))?.split('=')[1] ?? fallback);
const width = option('width', 1400);
const height = option('height', 900);
// The window's layout (oblada/browser/ui/sidebar_mode.h): --mode=scaffold or
// --mode=compact (or --compact).
const mode = args.find((a) => a.startsWith('--mode='))?.slice(7) ??
  (args.includes('--compact') ? 'compact' : 'sidebar');
const sidebarWidth = option('sidebar-width', 0);
const media = option('media', 0);
const actions = args.find((a) => a.startsWith('--do='))?.slice(5) ?? '';

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'oblada-screenshot-'));
const snapshots = path.join(tmp, 'snapshots');
fs.mkdirSync(snapshots);

const pages = {
  mail: ['Inbox – Mail', '#e8f0fe'],
  docs: ['Quarterly plan – Docs', '#e6f4ea'],
  news: ['Morning news', '#fef7e0'],
  bank: ['Accounts – Bank', '#fce8e6'],
};
// Music for --media (silence, see silentWav()), with what's playing and how far along.
const tracks = [
  ['Funiculì funiculà', 'Coro di Capri', 213, 37],
  ['Notte a Procida', 'Orchestra di Marechiaro', 1873, 114],
  ['Tarantella del Vomero', 'I Pescatori di Ischia', 158, 12],
];
const silence = silentWav();
const server = http.createServer((req, res) => {
  if (req.url === '/silence.wav') {
    res.setHeader('Content-Type', 'audio/wav');
    res.end(silence);
    return;
  }
  const track = req.url.match(/^\/\?track(\d)$/);
  if (track) {
    const [title, artist, duration, position] = tracks[track[1]];
    res.setHeader('Content-Type', 'text/html');
    res.end(`<!doctype html><meta charset="utf-8"><title>${title} – ${artist}</title>
      <body style="margin:0;background:#202124;color:#fff;font:32px system-ui;padding:48px">${title}
      <audio src="/silence.wav" loop></audio>
      <script>
        const audio = document.querySelector('audio');
        navigator.mediaSession.metadata = new MediaMetadata(
          { title: ${JSON.stringify(title)}, artist: ${JSON.stringify(artist)} });
        for (const action of ['previoustrack', 'nexttrack', 'seekto']) {
          navigator.mediaSession.setActionHandler(action, () => {});
        }
        audio.play().then(() => navigator.mediaSession.setPositionState(
          { duration: ${duration}, position: ${position}, playbackRate: 1 }));
      </script>`);
    return;
  }
  const [title, color] = pages[req.url.slice(2)] ?? ['Oblada', '#fff'];
  res.setHeader('Content-Type', 'text/html');
  res.end(`<!doctype html><meta charset="utf-8"><title>${title}</title>
    <body style="margin:0;background:${color};font:32px system-ui;padding:48px">${title}</body>`);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const site = `http://127.0.0.1:${server.address().port}/`;

// An extension, as most people have some: it puts the extensions button in the toolbar.
const extension = path.join(tmp, 'extension');
fs.mkdirSync(extension);
fs.writeFileSync(path.join(extension, 'manifest.json'), JSON.stringify(
  { manifest_version: 3, name: 'Example', version: '1.0', action: { default_popup: 'popup.html' } }));
fs.writeFileSync(path.join(extension, 'popup.html'),
  '<!doctype html><body style="font:16px system-ui;padding:16px;width:180px">Example popup</body>');

const verticalTabs = {
  ...(sidebarWidth && { uncollapsed_width: sidebarWidth }),
};
if (Object.keys(verticalTabs).length || mode !== 'sidebar') {
  fs.mkdirSync(path.join(tmp, 'profile', 'Default'), { recursive: true });
  fs.writeFileSync(path.join(tmp, 'profile', 'Default', 'Preferences'),
    JSON.stringify({ vertical_tabs: verticalTabs, oblada: { sidebar: { mode } } }));
}

const focus = watchFocus();
const oblada = await launch({
  profile: path.join(tmp, 'profile'),
  args: [`--oblada-snapshot-dir=${snapshots}`, `--window-size=${width},${height}`,
         `--load-extension=${extension}`,
         ...(media ? ['--autoplay-policy=no-user-gesture-required'] : [])],
});
try {
  const manager = await oblada.open('chrome://containers');
  await until('containers page', () => oblada.containers(manager, 'getState'));
  for (let i = 0; i < Math.min(media, tracks.length); i++) {
    await oblada.open(`${site}?track${i}`);
    await sleep(500);
  }
  await oblada.open(`${site}?news`);
  await oblada.containers(manager, 'openContainer', 'personal', `${site}?mail`);
  await oblada.containers(manager, 'openContainer', 'banking', `${site}?bank`);
  await oblada.containers(manager, 'openContainer', 'work', `${site}?docs`);
  await sleep(2000);
  await snapshot(snapshots, out, actions);
  console.log(out);
  // Frames of a "film:<count>:<ms>", next to it as <name>.film-<i>.png.
  for (const frame of fs.readdirSync(snapshots).filter((f) => /^film-\d+\.png$/.test(f))) {
    const copy = out.replace(/\.png$/, `.${frame}`);
    fs.copyFileSync(path.join(snapshots, frame), copy);
    console.log(copy);
  }
} finally {
  await oblada.kill();
  server.close();
  fs.rmSync(tmp, { recursive: true, force: true });
  if (focus.stop().tookFocus) console.log('WARNING: Oblada took focus');
}
