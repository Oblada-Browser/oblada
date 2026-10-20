#!/usr/bin/env node
// Takes a screenshot of Oblada's window, browser UI included, without ever showing it:
// headless Oblada with a throwaway profile, a few tabs open (some in containers).
//
//   node chromium/scripts/screenshot.mjs [out.png] [--width=1400] [--height=900]
//       [--compact] [--sidebar-width=280] [--do="<command ids, click:<view class>>"]
//
// --compact collapses the sidebar (compact mode); --sidebar-width sets the width the
// sidebar was left at; --do runs browser commands (e.g. 39001, focus the address bar),
// clicks views, "split"s the active tab or "drag:<n>:<x>,<y>"s a tab over the page
// before the screenshot (see oblada/browser/debug/window_snapshots.cc); "film:<count>:<ms>"
// also saves frames of what they do, <ms> apart, as <out>.film-<i>.png.
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { launch, sleep, snapshot, until, watchFocus } from './lib/oblada.mjs';

const args = process.argv.slice(2);
const out = path.resolve(args.find((a) => !a.startsWith('--')) ?? 'oblada.png');
const option = (name, fallback) =>
  Number(args.find((a) => a.startsWith(`--${name}=`))?.split('=')[1] ?? fallback);
const width = option('width', 1400);
const height = option('height', 900);
const compact = args.includes('--compact');
const sidebarWidth = option('sidebar-width', 0);
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
const server = http.createServer((req, res) => {
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
  ...(compact && { collapsed_state: true }),
  ...(sidebarWidth && { uncollapsed_width: sidebarWidth }),
};
if (Object.keys(verticalTabs).length) {
  fs.mkdirSync(path.join(tmp, 'profile', 'Default'), { recursive: true });
  fs.writeFileSync(path.join(tmp, 'profile', 'Default', 'Preferences'),
    JSON.stringify({ vertical_tabs: verticalTabs }));
}

const focus = watchFocus();
const oblada = await launch({
  profile: path.join(tmp, 'profile'),
  args: [`--oblada-snapshot-dir=${snapshots}`, `--window-size=${width},${height}`,
         `--load-extension=${extension}`],
});
try {
  const manager = await oblada.open('chrome://containers');
  await until('containers page', () => oblada.containers(manager, 'getState'));
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
