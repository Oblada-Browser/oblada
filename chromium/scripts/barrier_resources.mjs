// Writes the resources the Barrier's ad-blocking engine (adblock-rust) uses for $redirect
// rules and +js() scriptlets, in adblock-rust's format, as Brave's packager does
// (brave-core-crx-packager's lib/adBlockRustUtils.js):
// - uBlock Origin's scriptlets (src/js/resources/scriptlets.js), as functions;
// - uBlock Origin's redirect resources (src/web_accessible_resources, indexed by
//   src/js/redirect-resources.js);
// - Brave's own resources (adblock-resources' dist/resources.json).
//
//   node barrier_resources.mjs <uBlock Origin's sources> <Brave's resources.json> <out.json>
//
// Run by barrier_resources.sh, which fetches them.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [ubo, braveResources, out] = process.argv.slice(2);
if (!out) {
  console.error('usage: barrier_resources.mjs <uBlock dir> <brave resources.json> <out.json>');
  process.exit(2);
}

const base64 = (data) => Buffer.from(data).toString('base64');

// As adblock-rust's MimeType::from_extension().
const MIME_TYPES = {
  css: 'text/css',
  gif: 'image/gif',
  html: 'text/html',
  js: 'application/javascript',
  json: 'application/json',
  mp3: 'audio/mp3',
  mp4: 'video/mp4',
  png: 'image/png',
  txt: 'text/plain',
  xml: 'text/xml',
};
// Resources adblock-rust reads as text, without carriage returns.
const TEXTUAL = new Set(['application/javascript', 'text/html', 'text/plain']);

const resources = [];

// Redirect resources.
const { default: redirects } = await import(
  pathToFileURL(path.join(ubo, 'src/js/redirect-resources.js')).href);
for (const [name, { alias }] of redirects) {
  const mime = MIME_TYPES[name.split('.').pop()] ?? 'application/octet-stream';
  let content = fs.readFileSync(path.join(ubo, 'src/web_accessible_resources', name));
  if (TEXTUAL.has(mime)) {
    content = content.toString('utf8').replaceAll('\r', '');
  }
  resources.push({
    name,
    aliases: alias === undefined ? [] : [alias].flat(),
    kind: { mime },
    content: base64(content),
  });
}
// As Brave: this one breaks Google's IMA SDK for some sites.
const filtered = resources.filter((resource) => resource.name !== 'google-ima-dai.js');

filtered.push(...JSON.parse(fs.readFileSync(braveResources, 'utf8')));

// Scriptlets, uBlock Origin's own last so that they win over Brave's of the same name.
const { builtinScriptlets } = await import(
  pathToFileURL(path.join(ubo, 'src/js/resources/scriptlets.js')).href);
const names = new Set(builtinScriptlets.map((scriptlet) => scriptlet.name));
for (const scriptlet of builtinScriptlets) {
  for (const dependency of scriptlet.dependencies ?? []) {
    if (!names.has(dependency)) {
      throw new Error(`scriptlet ${scriptlet.name} needs ${dependency}, which isn't there`);
    }
  }
  filtered.push({
    name: scriptlet.name,
    aliases: scriptlet.aliases ?? [],
    kind: { mime: 'application/javascript' },
    content: base64(scriptlet.fn.toString()),
    ...(scriptlet.dependencies?.length ? { dependencies: scriptlet.dependencies } : {}),
    // Only lists trusted with them may inject uBlock Origin's trusted scriptlets
    // (adblock_engine.h's kTrusted).
    ...(scriptlet.requiresTrust ? { permission: 1 } : {}),
  });
}

fs.writeFileSync(out, JSON.stringify(filtered));
console.log(`${filtered.length} resources in ${out}`);
