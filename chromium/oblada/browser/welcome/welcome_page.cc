#include "oblada/browser/welcome/welcome_page.h"

namespace oblada::welcome {

// One step at a time, centered: the setup first, then each of the browser's
// features shown at work in a little window of its own.
const char kWelcomeHtml[] = R"oblada(<!doctype html>
<html lang="$i18n{lang}">
<meta charset="utf-8">
<meta name="color-scheme" content="light dark">
<title>$i18n{title}</title>
<link rel="stylesheet" href="chrome://theme/colors.css?sets=ui,chrome">
<link rel="stylesheet" href="welcome.css">
<script type="module" src="welcome.js"></script>
<body class="$platform">
<button class="skip" id="skip">$i18n{skip}</button>

<section class="step hero">
  <img class="logo" src="logo.png" alt="">
  <h1>$i18n{title}</h1>
  <p class="lead">$i18n{lead}</p>
  <button class="primary big" data-next>$i18n{start}</button>
</section>

<section class="step" id="import">
  <div class="fit"><div class="art importer">
    <div class="sources" id="sources"></div>
    <p class="none" id="no-sources" hidden>$i18n{import_none}</p>
  </div></div>
  <h2>$i18n{import_title}</h2>
  <p class="text" id="import-text">$i18n{import_text}</p>
  <div class="choices" id="choices"></div>
  <p class="note" id="import-note" hidden></p>
  <button class="primary" id="import-button" hidden></button>
  <p class="status" id="import-status"></p>
  <button class="link" id="import-file">$i18n{import_file}</button>
  <p class="status" id="file-status" hidden></p>
  <div id="strings" hidden data-text="$i18n{import_text}" data-from="$i18n{import_from}"
       data-running="$i18n{import_running}" data-imported="$i18n{import_done}"
       data-bookmarks="$i18n{import_bookmarks}" data-history="$i18n{import_history}"
       data-passwords="$i18n{import_passwords}" data-autofill="$i18n{import_autofill}"
       data-cards="$i18n{import_cards}" data-safari-how="$i18n{import_safari_how}"
       data-safari-choose="$i18n{import_safari_choose}" data-locked="$i18n{import_locked}"
       data-failed="$i18n{import_failed}" data-unreadable="$i18n{import_unreadable}"
       data-keychain="$i18n{import_keychain}"></div>
</section>

<section class="step">
  <div class="fit"><div class="art ct-panel">
    <div class="ct-grid" id="ct-grid"></div>
    <div class="ct-editor" id="ct-editor" hidden>
      <div class="ct-name-row">
        <span class="ct-badge big" id="ct-editor-badge"></span>
        <input id="ct-name" maxlength="40" placeholder="$i18n{containers_name}" spellcheck="false">
      </div>
      <div class="ct-swatches" id="ct-colors"></div>
      <div class="ct-icons" id="ct-icons"></div>
      <div class="ct-sites" id="ct-sites-row">
        <span class="ct-label">$i18n{containers_sites}</span>
        <span id="ct-sites"></span>
        <input id="ct-site" placeholder="$i18n{containers_add_site}" spellcheck="false">
      </div>
      <div class="ct-actions">
        <button class="link danger" id="ct-remove">$i18n{containers_remove}</button>
        <button class="primary small" id="ct-done">$i18n{containers_done}</button>
      </div>
    </div>
  </div></div>
  <h2>$i18n{containers_title}</h2>
  <p class="text">$i18n{containers_setup_text}</p>
  <div class="ct-firefox" id="ct-firefox"></div>
  <p class="status" id="ct-status"></p>
  <div id="ct-strings" hidden data-new="$i18n{containers_new}" data-add="$i18n{containers_add}"
       data-done="$i18n{containers_done}" data-from="$i18n{containers_import_from}"
       data-imported="$i18n{containers_imported}"></div>
</section>

<section class="step">
  <div class="fit"><div class="art">
    <div class="apps links"><span><b></b>procida.it</span><span><b></b>ischia.it</span><span><b></b>capri.com</span></div>
    <div class="flow"><i></i><i></i><i></i></div>
    <img class="dest" src="logo.png" alt="">
  </div></div>
  <h2>$i18n{default_title}</h2>
  <p class="text">$i18n{default_text}</p>
  <button class="primary" id="make-default">$i18n{default}</button>
  <p class="confirm" id="is-default" hidden>✓ $i18n{default_done}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-ws">
    <div class="tint"></div>
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side">
      <div class="addr"></div>
      <div class="ws-view"><div class="ws-track">
        <div class="ws"><div class="tab on"></div><div class="tab"></div><div class="tab"></div></div>
        <div class="ws"><div class="tab"></div><div class="tab"></div><div class="tab on"></div><div class="tab"></div></div>
      </div></div>
      <div class="ws-dots"><i></i><i></i></div>
    </div>
    <div class="page">
      <div class="pg a"><i class="l w70"></i><i class="l"></i><i class="l w50"></i><i class="l w80"></i></div>
      <div class="pg b"><i class="block"></i><i class="l w60"></i><i class="l"></i></div>
    </div>
    <div class="fingers"><i></i><i></i></div>
  </div></div>
  <h2>$i18n{workspaces}</h2>
  <p class="text">$i18n{workspaces_text}</p>
  <p class="hint">$i18n{workspaces_hint}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-ct">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side">
      <div class="addr"><b class="chip"></b></div>
      <div class="tab on edge-o"></div><div class="tab edge-b"></div><div class="tab"></div>
    </div>
    <div class="page">
      <div class="pane p1"><div class="band o">$i18n{work}</div>
        <div class="who"><i class="av o"></i><i class="l w40"></i></div><i class="l"></i><i class="l w70"></i></div>
      <div class="pane p2"><div class="band b">$i18n{personal}</div>
        <div class="who"><i class="av b"></i><i class="l w50"></i></div><i class="l w80"></i><i class="l w60"></i></div>
    </div>
  </div></div>
  <h2>$i18n{containers}</h2>
  <p class="text">$i18n{containers_text}</p>
  <a class="hint" href="chrome://containers/" target="_blank">$i18n{containers_hint} →</a>
</section>

<section class="step">
  <div class="fit"><div class="mock m-ab">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side"><div class="addr src"></div><div class="tab on"></div><div class="tab"></div><div class="tab"></div></div>
    <div class="page"><i class="l w70"></i><i class="l"></i><i class="l w50"></i><i class="l w80"></i><i class="l w60"></i></div>
    <div class="dim"></div>
    <div class="fly"></div>
    <div class="omni"><span class="q">$i18n{address_query}</span></div>
    <div class="sugg"><i></i><i></i><i></i></div>
  </div></div>
  <h2>$i18n{address_bar}</h2>
  <p class="text">$i18n{address_bar_text}</p>
  <p class="hint">$i18n{address_bar_hint}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-pk">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side"><div class="addr"></div><div class="tab on"></div><div class="tab"></div></div>
    <div class="page"><i class="l w70"></i><i class="l"></i><i class="gap"></i><i class="l w80"></i><i class="l w60"></i><i class="l"></i></div>
    <span class="link">$i18n{peek_link}</span>
    <div class="dim"></div>
    <div class="card"><i class="block"></i><i class="l w70"></i><i class="l"></i><i class="l w50"></i></div>
    <div class="pbtns"><i></i><i></i><i></i></div>
    <div class="key"><span class="only-mac">⌥</span><span class="not-mac">Alt</span></div>
    <svg class="cursor" viewBox="0 0 16 16"><path d="M2 1.5v11.5l3.2-3 2.2 5 2-.9-2.2-4.9h4.4L2 1.5Z"/></svg>
  </div></div>
  <h2>$i18n{peek}</h2>
  <p class="text">$i18n{peek_text}</p>
  <p class="hint">$i18n{peek_hint}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-sp">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side"><div class="addr"></div><div class="tab on"></div><div class="tab src"></div><div class="tab"></div></div>
    <div class="page left"><i class="l w70"></i><i class="l"></i><i class="l w50"></i><i class="l w80"></i></div>
    <div class="page right"><i class="block"></i><i class="l w60"></i><i class="l"></i></div>
    <div class="drop"></div>
    <div class="ghost"></div>
    <svg class="cursor" viewBox="0 0 16 16"><path d="M2 1.5v11.5l3.2-3 2.2 5 2-.9-2.2-4.9h4.4L2 1.5Z"/></svg>
  </div></div>
  <h2>$i18n{split}</h2>
  <p class="text">$i18n{split_text}</p>
  <p class="hint">$i18n{split_hint}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-cp">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="page"><i class="l w70"></i><i class="l"></i><i class="l w50"></i><i class="l w80"></i></div>
    <div class="side"><div class="addr"></div><div class="tab on"></div><div class="tab"></div><div class="tab"></div></div>
    <svg class="cursor" viewBox="0 0 16 16"><path d="M2 1.5v11.5l3.2-3 2.2 5 2-.9-2.2-4.9h4.4L2 1.5Z"/></svg>
  </div></div>
  <h2>$i18n{compact}</h2>
  <p class="text">$i18n{compact_text}</p>
  <p class="hint">$i18n{compact_hint}</p>
</section>

<section class="step">
  <div class="fit"><div class="mock m-ex">
    <div class="lights"><i></i><i></i><i></i></div>
    <div class="side"><div class="addr"><b class="x1"></b><b class="x2"></b></div><div class="tab on"></div><div class="tab"></div></div>
    <div class="page">
      <div class="store s1"><i class="logo cws"></i><span>Chrome Web Store</span><b class="add"></b></div>
      <div class="store s2"><i class="logo amo"></i><span>Firefox Add-ons</span><b class="add"></b><i class="convert"></i></div>
    </div>
    <div class="piece p1"></div><div class="piece p2"></div>
  </div></div>
  <h2>$i18n{extensions}</h2>
  <p class="text">$i18n{extensions_text}</p>
  <p class="hint"><a href="https://chromewebstore.google.com/" target="_blank">Chrome Web Store</a> · <a href="https://addons.mozilla.org/firefox/extensions/" target="_blank">Firefox Add-ons</a></p>
</section>

<section class="step hero">
  <img class="logo" src="logo.png" alt="">
  <h1>$i18n{done_title}</h1>
  <p class="lead">$i18n{zen} <a href="https://zen-browser.app/donate" target="_blank" rel="noopener">$i18n{zen_link}</a></p>
  <button class="primary big" id="finish">$i18n{finish}</button>
</section>

<nav class="bar">
  <button class="ghost-button" id="back">$i18n{back}</button>
  <div class="dots"></div>
  <button class="ghost-button" id="next">$i18n{next}</button>
</nav>
</body>
)oblada";

// The window's own colors (chrome://theme/colors.css), on its own background
// (the glass): veils as the sidebar's, the accent of the workspace shown.
const char kWelcomeCss[] = R"oblada(
:root {
  interpolate-size: allow-keywords;
  --text: var(--color-sys-on-surface, #1f1f1f);
  --subtle: var(--color-sys-on-surface-subtle, #5f6368);
  --veil: color-mix(in srgb, var(--text) 7%, transparent);
  --veil-hover: color-mix(in srgb, var(--text) 12%, transparent);
  --veil-strong: color-mix(in srgb, var(--text) 20%, transparent);
  --line: color-mix(in srgb, var(--text) 10%, transparent);
  --accent: var(--color-sys-primary, #0b57d0);
  --on-accent: var(--color-sys-on-primary, #fff);
  --page: light-dark(#ffffff, #26262b);
  --page-line: light-dark(#e7e9ed, #3a3a41);
  --solid: light-dark(#eef0f4, #2c2c33);
  --orange: #f0953a;
  --blue: #4a8cf5;
  --k: 1;
}
html, body {
  height: 100%;
  margin: 0;
  overflow: hidden;
  background: transparent;
}
body {
  color: var(--text);
  font: 13px/1.5 -apple-system, BlinkMacSystemFont, system-ui, sans-serif;
  -webkit-font-smoothing: antialiased;
  user-select: none;
}
[hidden] { display: none !important; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
button { font: inherit; color: inherit; }

/* Steps: one at a time, centered, sliding in. */
.step {
  position: absolute;
  inset: 0 0 76px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 0 32px;
  text-align: center;
  opacity: 0;
  transform: translateX(32px);
  transition: opacity .35s ease, transform .5s cubic-bezier(.2, .8, .2, 1);
  pointer-events: none;
}
.step.past { transform: translateX(-32px); }
.step.active { opacity: 1; transform: none; pointer-events: auto; }
.step.active > * { animation: rise .55s cubic-bezier(.2, .8, .2, 1) both; }
.step.active > :nth-child(2) { animation-delay: .05s; }
.step.active > :nth-child(3) { animation-delay: .1s; }
.step.active > :nth-child(4) { animation-delay: .15s; }
.step.active > :nth-child(5) { animation-delay: .2s; }
@keyframes rise { from { opacity: 0; transform: translateY(10px); } }

.logo { width: 88px; height: 88px; filter: drop-shadow(0 14px 24px rgba(0, 0, 0, .25)); }
.hero .logo { animation: float 6s ease-in-out infinite !important; }
@keyframes float { 50% { transform: translateY(-6px); } }
h1 { font-size: 34px; font-weight: 650; letter-spacing: -.02em; margin: 22px 0 8px; }
h2 { font-size: 24px; font-weight: 650; letter-spacing: -.01em; margin: 28px 0 6px; }
.lead { font-size: 15px; color: var(--subtle); max-width: 470px; margin: 0; }
.text { font-size: 14px; color: var(--subtle); max-width: 440px; margin: 0; }
.hint {
  margin: 16px 0 0;
  padding: 5px 12px;
  border-radius: 99px;
  background: var(--veil);
  font-size: 12px;
  font-weight: 500;
}
a.hint:hover { background: var(--veil-hover); text-decoration: none; }
.primary {
  display: inline-block;
  margin-top: 24px;
  padding: 10px 20px;
  border: 0;
  border-radius: 10px;
  background: var(--accent);
  color: var(--on-accent);
  font-weight: 600;
  cursor: pointer;
  transition: transform .12s, filter .12s;
}
.primary:hover { filter: brightness(1.08); text-decoration: none; }
.primary:active { transform: scale(.97); }
.primary:disabled { opacity: .55; cursor: default; filter: none; transform: none; }
.primary.big { padding: 12px 30px; font-size: 14px; border-radius: 12px; }
.confirm { margin: 24px 0 0; color: var(--accent); font-weight: 600; font-size: 14px; }

/* The import panel: the browsers found on this computer. */
.art.importer {
  height: auto;
  min-height: 150px;
  padding: 18px;
  box-sizing: border-box;
}
.sources { display: flex; flex-wrap: wrap; align-items: flex-start; justify-content: center; gap: 6px; }
.source {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 104px;
  padding: 10px 6px 8px;
  border: 0;
  border-radius: 12px;
  background: none;
  cursor: pointer;
  transition: background .15s, box-shadow .15s, transform .12s;
}
.source:hover { background: var(--veil); }
.source:active { transform: scale(.97); }
.source.selected { background: var(--veil-hover); box-shadow: inset 0 0 0 2px var(--accent); }
.source .icon { width: 44px; height: 44px; margin-bottom: 6px; border-radius: 11px; object-fit: contain; }
.source span.icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 38px;
  height: 38px;
  margin: 3px 3px 9px;
  background: var(--brand);
  color: #fff;
  font-size: 20px;
  font-weight: 700;
}
.source .name { font-size: 12.5px; font-weight: 600; }
.source .profile {
  max-width: 96px;
  min-height: 1.5em;
  overflow: hidden;
  color: var(--subtle);
  font-size: 11px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.source.running .icon { animation: working 1.2s ease-in-out infinite; }
@keyframes working { 50% { transform: scale(.88); opacity: .6; } }
.source.done::after {
  content: '✓';
  position: absolute;
  top: 7px;
  right: 22px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #34a853;
  color: #fff;
  font-size: 11px;
  font-weight: 700;
  line-height: 18px;
}
.none { margin: 0; color: var(--subtle); }
.choices { display: flex; flex-wrap: wrap; justify-content: center; gap: 6px; margin-top: 18px; }
.choices:empty { display: none; }
.choice {
  padding: 5px 12px;
  border: 0;
  border-radius: 99px;
  background: var(--veil);
  color: var(--subtle);
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
}
.choice.on { background: color-mix(in srgb, var(--accent) 20%, transparent); color: var(--text); }
.choice.on::before { content: '✓ '; color: var(--accent); }
.choice.static { cursor: default; }
.choice.static::before { content: none; }
.status { min-height: 1.5em; margin: 12px 0 0; color: var(--subtle); font-size: 12.5px; }
.note { max-width: 440px; margin: 12px 0 0; color: var(--subtle); font-size: 12.5px; }
.status.warn { color: light-dark(#b45309, #fbbf24); }
.status.good { color: var(--text); }
.link {
  margin-top: 2px;
  padding: 2px 6px;
  border: 0;
  background: none;
  color: var(--accent);
  font-size: 12px;
  cursor: pointer;
}
.link:hover { text-decoration: underline; }

/* Containers: Oblada's, to make one's own, and Firefox's, to bring along. */
.art.ct-panel {
  display: block;
  height: auto;
  min-height: 150px;
  padding: 18px;
  box-sizing: border-box;
}
.ct-grid { display: flex; flex-wrap: wrap; align-items: flex-start; justify-content: center; gap: 6px; }
.ct-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 7px;
  width: 96px;
  padding: 12px 4px 10px;
  border: 0;
  border-radius: 12px;
  background: none;
  cursor: pointer;
  transition: background .15s, transform .12s;
}
.ct-card:hover { background: var(--veil); }
.ct-card:active { transform: scale(.97); }
.ct-card .ct-name {
  max-width: 88px;
  overflow: hidden;
  font-size: 12.5px;
  font-weight: 600;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.ct-card.new .ct-badge { background: var(--veil); color: var(--subtle); font-size: 22px; font-weight: 300; }
.ct-card.new .ct-name { color: var(--subtle); font-weight: 500; }
.ct-card.fresh { animation: pop .5s cubic-bezier(.2, .8, .2, 1); }
@keyframes pop { from { transform: scale(.6); opacity: 0; } }
.ct-badge {
  display: flex;
  align-items: center;
  justify-content: center;
  flex: none;
  width: 40px;
  height: 40px;
  border-radius: 50%;
  background: var(--badge);
  box-shadow: 0 6px 14px -6px var(--badge);
}
.ct-badge.big { width: 34px; height: 34px; }
.ct-badge.small { width: 14px; height: 14px; box-shadow: none; }
.ct-mask {
  width: 20px;
  height: 20px;
  background: #fff;
  mask-size: contain;
  mask-repeat: no-repeat;
  mask-position: center;
}
.ct-badge.big .ct-mask { width: 18px; height: 18px; }
.ct-badge.small .ct-mask { width: 9px; height: 9px; }
.ct-editor { display: flex; flex-direction: column; gap: 12px; text-align: left; }
.ct-name-row { display: flex; align-items: center; gap: 10px; }
.ct-editor input {
  flex: 1;
  min-width: 0;
  padding: 7px 10px;
  border: 1px solid var(--line);
  border-radius: 8px;
  outline: none;
  background: var(--veil);
  color: var(--text);
  font: inherit;
  font-size: 13px;
}
.ct-editor input:focus { border-color: var(--accent); }
#ct-name { font-size: 15px; font-weight: 600; }
.ct-swatches { display: flex; flex-wrap: wrap; gap: 8px; }
.ct-swatch {
  width: 22px;
  height: 22px;
  padding: 0;
  border: 0;
  border-radius: 50%;
  background: var(--badge);
  cursor: pointer;
  transition: transform .12s;
}
.ct-swatch:hover { transform: scale(1.12); }
.ct-swatch.on { box-shadow: 0 0 0 2px var(--page), 0 0 0 4px var(--badge); }
.ct-icons { display: flex; flex-wrap: wrap; gap: 2px; }
.ct-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 30px;
  height: 30px;
  padding: 0;
  border: 0;
  border-radius: 8px;
  background: none;
  cursor: pointer;
}
.ct-icon:hover { background: var(--veil); }
.ct-icon.on { background: var(--veil-strong); }
.ct-icon .ct-mask { width: 17px; height: 17px; background: var(--text); }
.ct-icon .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--text); }
.ct-sites { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.ct-label { color: var(--subtle); font-size: 12px; }
.ct-site {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 4px 3px 10px;
  border-radius: 99px;
  background: var(--veil);
  font-size: 12px;
}
.ct-site button {
  width: 18px;
  height: 18px;
  padding: 0;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--subtle);
  cursor: pointer;
}
.ct-site button:hover { background: var(--veil-strong); color: var(--text); }
#ct-site { flex: 1 1 160px; padding: 4px 10px; border-radius: 99px; font-size: 12px; }
.ct-actions { display: flex; justify-content: space-between; align-items: center; }
.primary.small { margin: 0; padding: 7px 16px; font-size: 13px; }
.link.danger { color: light-dark(#c5221f, #f28b82); }
.ct-firefox { display: flex; flex-direction: column; align-items: center; gap: 10px; margin-top: 18px; }
.ct-firefox:empty { display: none; }
.ct-firefox .ct-found { display: flex; flex-wrap: wrap; justify-content: center; gap: 6px; }
.ct-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px 4px 5px;
  border-radius: 99px;
  background: var(--veil);
  font-size: 12px;
}
.ct-firefox .ghost-button { position: static; }

/* What differs between platforms: shortcuts, window controls. */
body.mac .not-mac, body:not(.mac) .only-mac { display: none; }
body:not(.mac) .lights { left: auto; right: 11px; }
body:not(.mac) .lights i { border-radius: 2px; background: var(--veil-strong); }

/* Navigation. */
.bar {
  position: absolute;
  left: 32px;
  right: 32px;
  bottom: 24px;
  height: 36px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.ghost-button {
  position: absolute;
  padding: 8px 16px;
  border: 0;
  border-radius: 9px;
  background: var(--veil);
  font-weight: 500;
  cursor: pointer;
}
.ghost-button:hover { background: var(--veil-hover); }
#back { left: 0; }
#next { right: 0; }
.dots { display: flex; gap: 6px; }
.dot {
  width: 6px;
  height: 6px;
  padding: 0;
  border: 0;
  border-radius: 99px;
  background: var(--veil-strong);
  cursor: pointer;
  transition: width .3s cubic-bezier(.2, .8, .2, 1), background .3s;
}
.dot.on { width: 20px; background: var(--accent); }
.skip {
  position: absolute;
  top: 18px;
  right: 24px;
  z-index: 1;
  padding: 4px 8px;
  border: 0;
  border-radius: 7px;
  background: none;
  color: var(--subtle);
  cursor: pointer;
}
.skip:hover { background: var(--veil); color: var(--text); }

/* Illustrations: little windows of Oblada, scaled to the window. */
.fit { position: relative; isolation: isolate; zoom: var(--k); }
.fit::before, .hero::before {
  content: '';
  position: absolute;
  z-index: -1;
  inset: 12% 8%;
  border-radius: 50%;
  background: var(--accent);
  filter: blur(70px);
  opacity: .16;
  pointer-events: none;
}
.hero::before {
  inset: auto;
  top: calc(50% - 170px);
  left: calc(50% - 120px);
  width: 240px;
  height: 200px;
  opacity: .2;
  animation: glow 6s ease-in-out infinite !important;
}
@keyframes glow { 50% { opacity: .1; transform: scale(1.1); } }
.mock, .art {
  position: relative;
  background-image: linear-gradient(color-mix(in srgb, var(--text) 4%, transparent), transparent);
  width: 460px;
  height: 270px;
  border-radius: 16px;
  overflow: hidden;
  background: var(--veil);
  border: 1px solid var(--line);
  box-shadow: 0 30px 60px -28px rgba(0, 0, 0, .5);
  cursor: pointer;
}
.mock i, .mock b, .art i, .art b { display: block; }
.lights { position: absolute; left: 11px; top: 10px; display: flex; gap: 4px; z-index: 3; }
.lights i { width: 7px; height: 7px; border-radius: 50%; background: #ff5f57; }
.lights i:nth-child(2) { background: #febc2e; }
.lights i:nth-child(3) { background: #28c840; }
.side {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 112px;
  padding: 28px 8px 8px;
  box-sizing: border-box;
  overflow: hidden;
  z-index: 2;
}
.addr { position: relative; height: 16px; border-radius: 6px; background: var(--veil); margin-bottom: 10px; }
.tab { position: relative; height: 15px; border-radius: 5px; margin-bottom: 4px; }
.tab::before, .tab::after {
  content: '';
  position: absolute;
  top: 4px;
  height: 7px;
  border-radius: 2.5px;
  background: var(--veil-strong);
}
.tab::before { left: 6px; width: 7px; }
.tab::after { left: 19px; right: 12px; height: 4px; top: 5.5px; }
.tab.on { background: var(--veil-strong); }
.page {
  position: absolute;
  left: 112px;
  top: 6px;
  right: 6px;
  bottom: 6px;
  border-radius: 10px;
  background: var(--page);
  overflow: hidden;
  padding: 8px 0;
  box-sizing: border-box;
}
.l { height: 6px; border-radius: 3px; background: var(--page-line); margin: 10px 16px 0; }
.w40 { width: 40%; } .w50 { width: 50%; } .w60 { width: 60%; }
.w70 { width: 70%; } .w80 { width: 80%; }
.block { height: 70px; margin: 8px 16px 0; border-radius: 8px;
         background: color-mix(in srgb, var(--accent) 22%, var(--page)); }
.cursor { position: absolute; width: 16px; height: 16px; z-index: 6;
          fill: var(--text); stroke: var(--page); stroke-width: 1; }
.dim { position: absolute; left: 112px; top: 6px; right: 6px; bottom: 6px;
       border-radius: 10px; background: #000; opacity: 0; z-index: 3; }

/* Import and default browser: things flowing into Oblada. */
.art { display: flex; align-items: center; justify-content: center; gap: 30px; cursor: default; }
.apps { display: flex; flex-direction: column; gap: 10px; }
.apps span {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  border-radius: 10px;
  background: var(--veil);
  font-weight: 600;
  text-align: left;
}
.apps.links span { font-weight: 500; color: var(--accent); }
.apps b { width: 14px; height: 14px; border-radius: 50%; flex: none; }
.apps .safari { background: radial-gradient(circle at 50% 50%, #fff 0 18%, transparent 19%), linear-gradient(#3bc2ff, #1a6ef5); }
.apps .chrome { background: radial-gradient(circle, #4285f4 0 30%, #fff 31% 40%, transparent 41%), conic-gradient(#ea4335 0 33%, #fbbc04 0 66%, #34a853 0); }
.apps .firefox { background: linear-gradient(135deg, #ffbd4f, #ff7139 45%, #e2275f 75%, #7542e5); }
.apps.links b { border-radius: 4px; }
.apps.links span:nth-child(1) b { background: #4a8cf5; }
.apps.links span:nth-child(2) b { background: #34a853; }
.apps.links span:nth-child(3) b { background: #f0953a; }
.flow { position: relative; width: 110px; height: 10px; }
.flow i {
  position: absolute;
  top: 2px;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--accent);
  animation: flow 1.8s ease-in-out infinite;
}
.flow i:nth-child(2) { animation-delay: .6s; }
.flow i:nth-child(3) { animation-delay: 1.2s; }
@keyframes flow {
  0% { left: 0; opacity: 0; }
  20% { opacity: 1; }
  80% { opacity: 1; }
  100% { left: 104px; opacity: 0; }
}
.dest { width: 72px; height: 72px; filter: drop-shadow(0 10px 18px rgba(0, 0, 0, .25)); }

/* Workspaces: swiping between two, the window taking each one's color. */
.m-ws .tint { position: absolute; inset: 0; background: rgba(74, 140, 245, .14); }
.m-ws .ws-view { width: 96px; overflow: hidden; }
.m-ws .ws-track { display: flex; width: 192px; }
.m-ws .ws { width: 96px; flex: none; }
.m-ws .ws-dots { position: absolute; left: 0; right: 0; bottom: 12px; display: flex;
                 justify-content: center; gap: 7px; }
.m-ws .ws-dots i { width: 7px; height: 7px; border-radius: 50%; background: var(--veil-strong); }
.m-ws .ws-dots i:first-child { background: var(--blue); }
.m-ws .pg { position: absolute; inset: 8px 0; }
.m-ws .pg.b { opacity: 0; }
.m-ws .fingers { position: absolute; left: 50px; top: 130px; display: flex; gap: 6px;
                 opacity: 0; z-index: 4; }
.m-ws .fingers i { width: 11px; height: 11px; border-radius: 50%; background: var(--text); opacity: .45; }
.run.m-ws .tint { animation: ws-tint 6s ease-in-out infinite; }
.run.m-ws .ws-track { animation: ws-slide 6s ease-in-out infinite; }
.run.m-ws .pg.a { animation: ws-a 6s ease-in-out infinite; }
.run.m-ws .pg.b { animation: ws-b 6s ease-in-out infinite; }
.run.m-ws .fingers { animation: ws-swipe 6s ease-in-out infinite; }
.run.m-ws .ws-dots i:first-child { animation: ws-dot1 6s ease-in-out infinite; }
.run.m-ws .ws-dots i:last-child { animation: ws-dot2 6s ease-in-out infinite; }
@keyframes ws-slide { 0%, 36% { transform: none; } 46%, 84% { transform: translateX(-96px); }
                      94%, 100% { transform: none; } }
@keyframes ws-tint { 0%, 36% { background: rgba(74, 140, 245, .14); }
                     46%, 84% { background: rgba(176, 96, 240, .16); }
                     94%, 100% { background: rgba(74, 140, 245, .14); } }
@keyframes ws-a { 0%, 38% { opacity: 1; } 44%, 86% { opacity: 0; } 92%, 100% { opacity: 1; } }
@keyframes ws-b { 0%, 38% { opacity: 0; } 44%, 86% { opacity: 1; } 92%, 100% { opacity: 0; } }
@keyframes ws-swipe { 0%, 28% { opacity: 0; transform: translateX(20px); }
                      32% { opacity: 1; transform: translateX(20px); }
                      44% { opacity: 1; transform: translateX(-28px); }
                      50%, 100% { opacity: 0; transform: translateX(-28px); } }
@keyframes ws-dot1 { 0%, 38% { background: var(--blue); } 44%, 86% { background: var(--veil-strong); }
                     92%, 100% { background: var(--blue); } }
@keyframes ws-dot2 { 0%, 38% { background: var(--veil-strong); } 44%, 86% { background: #b060f0; }
                     92%, 100% { background: var(--veil-strong); } }

/* Containers: one site, two accounts, side by side. */
.m-ct .addr .chip { position: absolute; right: 4px; top: 4px; width: 22px; height: 8px;
                    border-radius: 4px; background: var(--orange); }
.m-ct .edge-o::before { background: var(--orange); }
.m-ct .edge-b::before { background: var(--blue); }
.m-ct .page { display: flex; gap: 6px; padding: 0; background: none; overflow: visible; }
.m-ct .pane { flex: 1; min-width: 0; border-radius: 10px; background: var(--page); overflow: hidden; }
.m-ct .band { height: 22px; padding-left: 12px; font-size: 10px; font-weight: 600; line-height: 22px; color: #fff;
              text-align: left; white-space: nowrap; }
.m-ct .band.o { background: var(--orange); }
.m-ct .band.b { background: var(--blue); }
.m-ct .who { display: flex; align-items: center; margin: 12px 16px 2px; }
.m-ct .who .l { margin: 0 0 0 8px; flex: 1; }
.m-ct .av { width: 20px; height: 20px; border-radius: 50%; flex: none; }
.m-ct .av.o { background: color-mix(in srgb, var(--orange) 70%, var(--page)); }
.m-ct .av.b { background: color-mix(in srgb, var(--blue) 70%, var(--page)); }
.run.m-ct .p2 { animation: ct-in 6s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-ct .chip { animation: ct-chip 6s ease-in-out infinite; }
@keyframes ct-in { 0%, 18% { flex-grow: .0001; opacity: 0; } 34%, 86% { flex-grow: 1; opacity: 1; }
                   96%, 100% { flex-grow: .0001; opacity: 0; } }
@keyframes ct-chip { 0%, 40% { background: var(--orange); } 46%, 86% { background: var(--blue); }
                     92%, 100% { background: var(--orange); } }

/* The address bar: from the sidebar to the middle of the page. */
.m-ab .fly, .m-ab .omni {
  position: absolute;
  left: 158px;
  top: 84px;
  width: 250px;
  height: 30px;
  border-radius: 10px;
  z-index: 4;
}
.m-ab .fly { background: var(--solid); opacity: 0; transform-origin: 0 0; }
.m-ab .omni {
  display: flex;
  align-items: center;
  padding: 0 12px;
  box-sizing: border-box;
  background: var(--solid);
  border: 1px solid var(--line);
  box-shadow: 0 14px 30px -8px rgba(0, 0, 0, .5);
  opacity: 0;
  font-size: 11.5px;
  text-align: left;
}
.m-ab .q { display: inline-block; overflow: hidden; white-space: nowrap; width: 0;
           border-right: 1px solid var(--accent); }
.m-ab .sugg { position: absolute; left: 158px; top: 118px; width: 250px; z-index: 4; opacity: 0;
              border-radius: 9px; background: var(--solid); padding: 4px 0; }
.m-ab .sugg i { height: 5px; margin: 7px 12px; border-radius: 3px; background: var(--veil-strong); }
.m-ab .sugg i:nth-child(1) { width: 70%; } .m-ab .sugg i:nth-child(2) { width: 52%; }
.m-ab .sugg i:nth-child(3) { width: 61%; }
.run.m-ab .fly { animation: ab-fly 6s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-ab .omni { animation: ab-omni 6s ease infinite; }
.run.m-ab .q { animation: ab-type 6s steps(22) infinite; }
.run.m-ab .sugg { animation: ab-sugg 6s ease infinite; }
.run.m-ab .dim { animation: ab-dim 6s ease-in-out infinite; }
.run.m-ab .src { animation: ab-src 6s ease infinite; }
@keyframes ab-fly { 0%, 12% { opacity: 0; transform: translate(-150px, -56px) scale(.384, .533); }
                    14% { opacity: 1; transform: translate(-150px, -56px) scale(.384, .533); }
                    32% { opacity: 1; transform: none; } 36%, 100% { opacity: 0; transform: none; } }
@keyframes ab-omni { 0%, 31% { opacity: 0; } 34%, 90% { opacity: 1; } 96%, 100% { opacity: 0; } }
@keyframes ab-type { 0%, 36% { width: 0; } 62%, 90% { width: max-content; } 96%, 100% { width: 0; } }
@keyframes ab-sugg { 0%, 62% { opacity: 0; transform: translateY(-4px); }
                     68%, 90% { opacity: 1; transform: none; } 96%, 100% { opacity: 0; } }
@keyframes ab-dim { 0%, 14% { opacity: 0; } 32%, 90% { opacity: .3; } 98%, 100% { opacity: 0; } }
@keyframes ab-src { 0%, 13% { opacity: 1; } 15%, 92% { opacity: .35; } 98%, 100% { opacity: 1; } }

/* Peek: Option-click a link, and it opens in a card over its page. */
.m-pk .gap { height: 18px; }
.m-pk .link { position: absolute; left: 128px; top: 62px; font-size: 10.5px; font-weight: 600;
              color: var(--accent); text-decoration: underline; z-index: 2; }
.m-pk .card { position: absolute; left: 128px; top: 18px; width: 296px; height: 236px;
              border-radius: 10px; background: var(--page); z-index: 4; opacity: 0;
              box-shadow: 0 18px 40px -10px rgba(0, 0, 0, .5); transform-origin: 42px 50px;
              padding-top: 4px; box-sizing: border-box; }
.m-pk .pbtns { position: absolute; left: 432px; top: 18px; display: flex; flex-direction: column;
               gap: 6px; z-index: 4; opacity: 0; }
.m-pk .pbtns i { width: 16px; height: 16px; border-radius: 50%; background: var(--solid); }
.m-pk .key { position: absolute; left: 186px; top: 36px; padding: 2px 6px; border-radius: 5px;
             background: var(--solid); border: 1px solid var(--line); font-size: 11px; z-index: 6;
             opacity: 0; }
.m-pk .cursor { left: 330px; top: 200px; }
.run.m-pk .cursor { animation: pk-cursor 6s cubic-bezier(.4, 0, .2, 1) infinite; }
.run.m-pk .key { animation: pk-key 6s ease infinite; }
.run.m-pk .card { animation: pk-card 6s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-pk .pbtns { animation: pk-btns 6s ease infinite; }
.run.m-pk .dim { animation: pk-dim 6s ease infinite; }
@keyframes pk-cursor { 0%, 4% { transform: none; opacity: 1; } 24%, 36% { transform: translate(-160px, -132px); }
                       40% { transform: translate(-160px, -132px); opacity: 0; } 100% { opacity: 0; } }
@keyframes pk-key { 0%, 24% { opacity: 0; transform: scale(.6); } 28%, 38% { opacity: 1; transform: none; }
                    42%, 100% { opacity: 0; } }
@keyframes pk-card { 0%, 34% { opacity: 0; transform: scale(.06); } 37% { opacity: 1; }
                     54%, 88% { opacity: 1; transform: none; } 96%, 100% { opacity: 0; transform: scale(.96); } }
@keyframes pk-btns { 0%, 52% { opacity: 0; transform: translateX(-6px); } 58%, 88% { opacity: 1; transform: none; }
                     94%, 100% { opacity: 0; } }
@keyframes pk-dim { 0%, 34% { opacity: 0; } 52%, 88% { opacity: .3; } 96%, 100% { opacity: 0; } }

/* Split view: a tab dragged onto the page goes next to it. */
.m-sp .page.left { right: 6px; }
.m-sp .page.right { left: 289px; opacity: 0; transform: scale(.94); }
.m-sp .drop { position: absolute; left: 289px; top: 6px; right: 6px; bottom: 6px; border-radius: 10px;
              border: 2px dashed var(--accent); box-sizing: border-box; opacity: 0; z-index: 3; }
.m-sp .ghost { position: absolute; left: 8px; top: 73px; width: 96px; height: 15px; border-radius: 5px;
               background: var(--veil-strong); opacity: 0; z-index: 5; }
.m-sp .cursor { left: 60px; top: 80px; opacity: 0; }
.run.m-sp .page.left { animation: sp-left 6s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-sp .page.right { animation: sp-right 6s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-sp .drop { animation: sp-drop 6s ease infinite; }
.run.m-sp .ghost { animation: sp-ghost 6s cubic-bezier(.4, 0, .2, 1) infinite; }
.run.m-sp .cursor { animation: sp-cursor 6s cubic-bezier(.4, 0, .2, 1) infinite; }
.run.m-sp .src { animation: sp-src 6s ease infinite; }
@keyframes sp-ghost { 0%, 8% { opacity: 0; transform: none; } 12% { opacity: .9; transform: none; }
                      40% { opacity: .9; transform: translate(240px, 40px); }
                      46%, 100% { opacity: 0; transform: translate(240px, 40px); } }
@keyframes sp-cursor { 0%, 8% { opacity: 0; transform: none; } 12% { opacity: 1; transform: none; }
                       40% { opacity: 1; transform: translate(240px, 40px); }
                       50%, 100% { opacity: 0; transform: translate(240px, 40px); } }
@keyframes sp-drop { 0%, 26% { opacity: 0; } 32%, 44% { opacity: 1; } 52%, 100% { opacity: 0; } }
@keyframes sp-left { 0%, 44% { right: 6px; } 58%, 88% { right: 177px; } 96%, 100% { right: 6px; } }
@keyframes sp-right { 0%, 44% { opacity: 0; transform: scale(.94); } 58%, 88% { opacity: 1; transform: none; }
                      96%, 100% { opacity: 0; transform: scale(.94); } }
@keyframes sp-src { 0%, 10% { opacity: 1; } 12%, 90% { opacity: 0; } 96%, 100% { opacity: 1; } }

/* Compact mode: the sidebar folds away, and comes back over the page. */
.m-cp .cursor { left: 330px; top: 150px; opacity: 0; }
.run.m-cp .side { animation: cp-side 7s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-cp .page { animation: cp-page 7s cubic-bezier(.2, .8, .2, 1) infinite; }
.run.m-cp .addr, .run.m-cp .lights { animation: cp-top 7s ease infinite; }
.run.m-cp .cursor { animation: cp-cursor 7s cubic-bezier(.4, 0, .2, 1) infinite; }
@keyframes cp-side {
  0%, 10% { width: 112px; background: transparent; box-shadow: none; }
  20%, 40% { width: 36px; background: transparent; box-shadow: none; }
  48%, 62% { width: 112px; background: var(--solid); box-shadow: 12px 0 30px -12px rgba(0, 0, 0, .45); }
  70%, 80% { width: 36px; background: transparent; box-shadow: none; }
  90%, 100% { width: 112px; background: transparent; box-shadow: none; }
}
@keyframes cp-page { 0%, 10% { left: 112px; } 20%, 80% { left: 36px; } 90%, 100% { left: 112px; } }
@keyframes cp-top { 0%, 10% { opacity: 1; } 18%, 44% { opacity: 0; } 50%, 60% { opacity: 1; }
                    68%, 82% { opacity: 0; } 90%, 100% { opacity: 1; } }
@keyframes cp-cursor { 0%, 28% { opacity: 0; transform: none; } 32% { opacity: 1; transform: none; }
                       44% { opacity: 1; transform: translate(-318px, -10px); }
                       58% { opacity: 1; transform: translate(-318px, -10px); }
                       68% { opacity: 1; transform: none; } 74%, 100% { opacity: 0; transform: none; } }

/* Extensions: from both stores. */
.m-ex .addr .x1, .m-ex .addr .x2 { position: absolute; top: 3px; width: 10px; height: 10px;
                                   border-radius: 3px; opacity: 0; }
.m-ex .addr .x1 { right: 18px; background: #e33f3f; }
.m-ex .addr .x2 { right: 4px; background: #3f7be3; }
.m-ex .page { display: flex; flex-direction: column; justify-content: center; gap: 14px; padding: 0 22px; }
.m-ex .store { position: relative; display: flex; align-items: center; gap: 10px; height: 46px;
               padding: 0 12px; border-radius: 10px; background: var(--veil); text-align: left;
               font-weight: 600; font-size: 11.5px; }
.m-ex .store span { flex: 1; }
.m-ex .logo { width: 24px; height: 24px; border-radius: 50%; flex: none; }
.m-ex .logo.cws { background: conic-gradient(#ea4335 0 33%, #fbbc04 0 66%, #34a853 0); }
.m-ex .logo.amo { background: linear-gradient(135deg, #ff9500, #e2275f 60%, #7542e5); }
.m-ex .add { width: 38px; height: 16px; border-radius: 8px; background: var(--accent); }
.m-ex .convert { position: absolute; left: 46px; bottom: 7px; height: 3px; width: 0; border-radius: 2px;
                 background: var(--accent); }
.m-ex .piece { position: absolute; width: 14px; height: 14px; border-radius: 4px; z-index: 5; opacity: 0; }
.m-ex .piece.p1 { left: 394px; top: 98px; background: #e33f3f; }
.m-ex .piece.p2 { left: 394px; top: 158px; background: #3f7be3; }
.run.m-ex .s1 .add { animation: ex-add1 6s ease infinite; }
.run.m-ex .s2 .add { animation: ex-add2 6s ease infinite; }
.run.m-ex .convert { animation: ex-convert 6s ease infinite; }
.run.m-ex .p1 { animation: ex-p1 6s cubic-bezier(.4, 0, .2, 1) infinite; }
.run.m-ex .p2 { animation: ex-p2 6s cubic-bezier(.4, 0, .2, 1) infinite; }
.run.m-ex .x1 { animation: ex-x1 6s ease infinite; }
.run.m-ex .x2 { animation: ex-x2 6s ease infinite; }
@keyframes ex-add1 { 0%, 12% { background: var(--accent); } 16%, 92% { background: #34a853; } 100% { background: var(--accent); } }
@keyframes ex-add2 { 0%, 50% { background: var(--accent); } 54%, 92% { background: #34a853; } 100% { background: var(--accent); } }
@keyframes ex-convert { 0%, 34% { width: 0; opacity: 1; } 50% { width: 120px; opacity: 1; } 54%, 100% { width: 120px; opacity: 0; } }
@keyframes ex-p1 { 0%, 14% { opacity: 0; transform: none; } 17% { opacity: 1; transform: none; }
                   32% { opacity: 1; transform: translate(-318px, -67px); } 34%, 100% { opacity: 0; transform: translate(-318px, -67px); } }
@keyframes ex-p2 { 0%, 52% { opacity: 0; transform: none; } 55% { opacity: 1; transform: none; }
                   70% { opacity: 1; transform: translate(-304px, -127px); } 72%, 100% { opacity: 0; transform: translate(-304px, -127px); } }
@keyframes ex-x1 { 0%, 31% { opacity: 0; } 34%, 92% { opacity: 1; } 100% { opacity: 0; } }
@keyframes ex-x2 { 0%, 69% { opacity: 0; } 72%, 92% { opacity: 1; } 100% { opacity: 0; } }

@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { animation: none !important; transition: none !important; }
}
)oblada";

const char kWelcomeJs[] = R"oblada(
import {addWebUiListener, sendWithPromise} from 'chrome://resources/js/cr.js';

const steps = [...document.querySelectorAll('.step')];
const dots = document.querySelector('.dots');
const back = document.getElementById('back');
const next = document.getElementById('next');
const skip = document.getElementById('skip');
let current = 0;

for (const [index] of steps.entries()) {
  const dot = document.createElement('button');
  dot.className = 'dot';
  dot.setAttribute('aria-label', String(index + 1));
  dot.addEventListener('click', () => show(index));
  dots.append(dot);
}

// Restarts an illustration's animations.
function replay(illustration) {
  illustration.classList.remove('run');
  void illustration.offsetWidth;
  illustration.classList.add('run');
}

function show(index) {
  current = Math.max(0, Math.min(steps.length - 1, index));
  steps.forEach((step, i) => {
    step.classList.toggle('active', i === current);
    step.classList.toggle('past', i < current);
    step.inert = i !== current;
  });
  [...dots.children].forEach((dot, i) => dot.classList.toggle('on', i === current));
  for (const mock of steps[current].querySelectorAll('.mock')) {
    replay(mock);
  }
  const last = current === steps.length - 1;
  back.hidden = current === 0;
  next.hidden = current === 0 || last;
  skip.hidden = current === 0 || last;
  fit();
}

next.addEventListener('click', () => show(current + 1));
back.addEventListener('click', () => show(current - 1));
skip.addEventListener('click', () => show(steps.length - 1));
for (const button of document.querySelectorAll('[data-next]')) {
  button.addEventListener('click', () => show(current + 1));
}
for (const mock of document.querySelectorAll('.mock')) {
  mock.addEventListener('click', () => replay(mock));
}
document.addEventListener('keydown', (event) => {
  if (event.key === 'ArrowRight') {
    show(current + 1);
  } else if (event.key === 'ArrowLeft') {
    show(current - 1);
  }
});
document.getElementById('finish').addEventListener('click', () => {
  chrome.send('finish');
});

// The default browser: whether Oblada is, and making it so.
const makeDefault = document.getElementById('make-default');
const isDefault = document.getElementById('is-default');
function showDefault(yes) {
  makeDefault.hidden = yes;
  isDefault.hidden = !yes;
}
function checkDefault() {
  return sendWithPromise('getDefaultBrowser').then((yes) => {
    showDefault(yes);
    return yes;
  });
}
checkDefault();
// The system may ask the user to confirm (macOS), or open its own settings for
// it (Windows): checks again until they have answered.
makeDefault.addEventListener('click', async () => {
  if (await sendWithPromise('setAsDefaultBrowser')) {
    showDefault(true);
    return;
  }
  for (let i = 0; i < 60 && !(await checkDefault()); i++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
});
addEventListener('focus', checkDefault);

// Containers: the profile's, to set up (with oblada://containers' messages),
// and Firefox's, to bring along.
const ct = document.getElementById('ct-strings').dataset;
const ctGrid = document.getElementById('ct-grid');
const ctEditor = document.getElementById('ct-editor');
const ctName = document.getElementById('ct-name');
const ctSite = document.getElementById('ct-site');
const ctStatus = document.getElementById('ct-status');
let ctState = null;
// The container being edited: one of ctState's, or {id: null} for a new one.
let ctEditing = null;
let ctFresh = null;

function ctBadge(css, iconId, size = '') {
  const badge = document.createElement('span');
  badge.className = `ct-badge ${size}`;
  badge.style.setProperty('--badge', css);
  const icon = ctState?.icons.find((i) => i.id === iconId);
  if (icon) {
    const mask = document.createElement('span');
    mask.className = 'ct-mask';
    mask.style.maskImage = `url("${icon.mask}")`;
    badge.append(mask);
  }
  return badge;
}

function ctUpdate(promise) {
  return promise.then((state) => {
    ctState = state;
    if (ctEditing?.id) {
      ctEditing = state.containers.find((c) => c.id === ctEditing.id) ?? null;
    }
    ctRender();
  });
}

function ctRender() {
  if (!ctState) {
    return;
  }
  const containers = ctState.containers.filter((c) => !c.temporary);
  ctGrid.hidden = !!ctEditing;
  ctEditor.hidden = !ctEditing;
  if (!ctEditing) {
    ctGrid.replaceChildren(...containers.map((container) => {
      const card = document.createElement('button');
      card.className = 'ct-card' + (container.id === ctFresh ? ' fresh' : '');
      const name = document.createElement('span');
      name.className = 'ct-name';
      name.textContent = container.name;
      card.append(ctBadge(container.css, container.icon), name);
      card.addEventListener('click', () => {
        ctEditing = container;
        ctRender();
      });
      return card;
    }));
    const add = document.createElement('button');
    add.className = 'ct-card new';
    const plus = document.createElement('span');
    plus.className = 'ct-badge';
    plus.textContent = '+';
    const label = document.createElement('span');
    label.className = 'ct-name';
    label.textContent = ct.new;
    add.append(plus, label);
    add.addEventListener('click', () => {
      const used = new Set(containers.map((c) => c.color));
      const color = ctState.colors.find((c) => !used.has(c.name)) ?? ctState.colors[0];
      ctEditing = {id: null, name: '', color: color.name, css: color.css, icon: 'circle', sites: []};
      ctRender();
      ctName.focus();
    });
    ctGrid.append(add);
    ctFresh = null;
    fit();
    return;
  }
  // The editor.
  const editing = ctEditing;
  const save = (changes) => {
    Object.assign(editing, changes);
    if (changes.color) {
      editing.css = ctState.colors.find((c) => c.name === changes.color).css;
    }
    if (editing.id && editing.name.trim()) {
      ctUpdate(sendWithPromise('updateContainer', editing.id, editing.name.trim(),
                               editing.color, editing.icon));
    } else {
      ctRender();
    }
  };
  document.getElementById('ct-editor-badge').replaceWith(
      Object.assign(ctBadge(editing.css, editing.icon, 'big'), {id: 'ct-editor-badge'}));
  if (document.activeElement !== ctName) {
    ctName.value = editing.name;
  }
  document.getElementById('ct-colors').replaceChildren(...ctState.colors.map((color) => {
    const swatch = document.createElement('button');
    swatch.className = 'ct-swatch' + (color.name === editing.color ? ' on' : '');
    swatch.style.setProperty('--badge', color.css);
    swatch.addEventListener('click', () => save({color: color.name}));
    return swatch;
  }));
  document.getElementById('ct-icons').replaceChildren(
      ...[...ctState.icons.map((i) => i.id), 'circle'].map((id) => {
        const button = document.createElement('button');
        button.className = 'ct-icon' + ((id === editing.icon ||
            (id === 'circle' && !ctState.icons.some((i) => i.id === editing.icon))) ? ' on' : '');
        const icon = ctState.icons.find((i) => i.id === id);
        const glyph = document.createElement('span');
        glyph.className = icon ? 'ct-mask' : 'dot';
        if (icon) {
          glyph.style.maskImage = `url("${icon.mask}")`;
        }
        button.append(glyph);
        button.addEventListener('click', () => save({icon: id}));
        return button;
      }));
  document.getElementById('ct-sites-row').hidden = !editing.id;
  document.getElementById('ct-sites').replaceChildren(...(editing.sites ?? []).map((site) => {
    const chip = document.createElement('span');
    chip.className = 'ct-site';
    chip.textContent = site;
    const forget = document.createElement('button');
    forget.textContent = '×';
    forget.addEventListener('click', () => ctUpdate(sendWithPromise('assignSite', site, '')));
    chip.append(forget);
    return chip;
  }));
  document.getElementById('ct-remove').hidden = !editing.id;
  document.getElementById('ct-done').textContent = editing.id ? ct.done : ct.add;
  fit();
}

ctName.addEventListener('input', () => {
  ctEditing.name = ctName.value;
});
ctName.addEventListener('change', () => {
  if (ctEditing?.id && ctName.value.trim()) {
    ctUpdate(sendWithPromise('updateContainer', ctEditing.id, ctName.value.trim(),
                             ctEditing.color, ctEditing.icon));
  }
});
ctName.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    document.getElementById('ct-done').click();
  }
  event.stopPropagation();
});
ctSite.addEventListener('keydown', (event) => {
  event.stopPropagation();
  if (event.key === 'Enter' && ctSite.value.trim() && ctEditing?.id) {
    ctUpdate(sendWithPromise('assignSite', ctSite.value.trim(), ctEditing.id));
    ctSite.value = '';
  }
});
document.getElementById('ct-done').addEventListener('click', () => {
  const editing = ctEditing;
  const name = ctName.value.trim();
  if (!editing.id) {
    if (!name) {
      ctName.focus();
      return;
    }
    const before = new Set(ctState.containers.map((c) => c.id));
    ctEditing = null;
    ctUpdate(sendWithPromise('addContainer', name, editing.color, editing.icon).then((state) => {
      ctFresh = state.containers.find((c) => !before.has(c.id))?.id ?? null;
      return state;
    }));
    return;
  }
  ctEditing = null;
  ctUpdate(name && name !== editing.name ?
      sendWithPromise('updateContainer', editing.id, name, editing.color, editing.icon) :
      sendWithPromise('getState'));
});
document.getElementById('ct-remove').addEventListener('click', () => {
  if (!confirm(ctState.strings.remove_confirm.replace('$1', ctEditing.name))) {
    return;
  }
  const id = ctEditing.id;
  ctEditing = null;
  ctUpdate(sendWithPromise('removeContainer', id));
});

ctUpdate(sendWithPromise('getState'));

// Firefox's containers, if there are any.
sendWithPromise('getFirefoxContainers').then((found) => {
  const view = document.getElementById('ct-firefox');
  for (const source of found) {
    const chips = document.createElement('div');
    chips.className = 'ct-found';
    chips.append(...source.containers.map((container) => {
      const chip = document.createElement('span');
      chip.className = 'ct-chip';
      chip.append(ctBadge(container.css, null, 'small'), container.name);
      return chip;
    }));
    const button = document.createElement('button');
    button.className = 'ghost-button';
    button.textContent = fill(ct.from,
        source.profile ? `${source.browser} (${source.profile})` : source.browser);
    button.addEventListener('click', async () => {
      button.disabled = true;
      const count = await sendWithPromise('importFirefoxContainers', source.id);
      ctStatus.textContent = `✓ ${fill(ct.imported, count)}`;
      ctStatus.className = 'status good';
      button.remove();
      chips.remove();
      await ctUpdate(sendWithPromise('getState'));
    });
    view.append(chips, button);
  }
  fit();
});

// Importing from the other browsers on this computer: a tile for each, what to
// bring from the one selected, and how it went.
const t = document.getElementById('strings').dataset;
const sourcesView = document.getElementById('sources');
const choicesView = document.getElementById('choices');
const importText = document.getElementById('import-text');
const importButton = document.getElementById('import-button');
const importStatus = document.getElementById('import-status');
const importNote = document.getElementById('import-note');
const fileStatus = document.getElementById('file-status');
const numbers = new Intl.NumberFormat(document.documentElement.lang);
// In the order they are listed.
const itemNames = {
  bookmarks: t.bookmarks,
  history: t.history,
  passwords: t.passwords,
  autofill: t.autofill,
  cards: t.cards,
};
const inOrder = (counts) =>
    Object.keys(itemNames).filter((item) => item in counts).map((item) => [item, counts[item]]);
let sources = [];
let selected = null;
// The last news from each source's import: {state, counts, unreadable}.
const progress = new Map();

function fill(text, value) {
  return text.replace('$1', value);
}
const brandColors = {
  Arc: '#f2545b',
  Brave: '#fb542b',
  Chrome: '#1a73e8',
  Chromium: '#4285f4',
  Edge: '#0c8ce9',
  Firefox: '#ff7139',
  Opera: '#ff1b2d',
  Safari: '#1e90ff',
  Vivaldi: '#ef3939',
};

function tile(source) {
  const button = document.createElement('button');
  button.className = 'source';
  let icon;
  if (source.icon) {
    icon = document.createElement('img');
    icon.src = source.icon;
    icon.alt = '';
  } else {
    // The browser's initial, in its color.
    icon = document.createElement('span');
    icon.textContent = source.browser[0];
    const hue = [...source.browser].reduce((h, c) => h * 31 + c.charCodeAt(0), 7) % 360;
    icon.style.setProperty('--brand', brandColors[source.browser] ?? `hsl(${hue} 50% 48%)`);
  }
  icon.classList.add('icon');
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = source.browser;
  const profile = document.createElement('span');
  profile.className = 'profile';
  profile.textContent = source.profile;
  button.append(icon, name, profile);
  button.addEventListener('click', () => {
    selected = source;
    render();
  });
  source.tile = button;
  return button;
}

function pill(text, on, toggle) {
  const button = document.createElement('button');
  button.className = 'choice';
  button.classList.toggle('on', on);
  button.classList.toggle('static', !toggle);
  button.textContent = text;
  if (toggle) {
    button.addEventListener('click', toggle);
  }
  return button;
}

// "Bookmarks 312 · History 4,120": what an import brought, or found.
function summary(news) {
  const parts = inOrder(news.counts)
      .filter(([, n]) => n !== 0)
      .map(([item, n]) => n < 0 ? itemNames[item] : `${itemNames[item]} ${numbers.format(n)}`);
  if (news.unreadable) {
    parts.push(fill(t.unreadable, numbers.format(news.unreadable)));
  }
  return parts.join(' · ');
}

function render() {
  for (const source of sources) {
    const state = progress.get(source.id)?.state;
    source.tile.classList.toggle('selected', source === selected);
    source.tile.classList.toggle('running', state === 'running');
    source.tile.classList.toggle('done', state === 'done');
  }
  choicesView.replaceChildren();
  importStatus.textContent = '';
  importStatus.className = 'status';
  importButton.hidden = !selected;
  importNote.hidden = true;
  if (!selected) {
    fit();
    return;
  }
  const news = progress.get(selected.id) ?? {state: 'idle', counts: {}};
  const safari = selected.family === 'safari';
  const waitingForFile = safari && !['found', 'running', 'done'].includes(news.state);
  importText.textContent = waitingForFile ? t.safariHow : t.text;
  importButton.disabled = news.state === 'running' || news.state === 'done';
  importButton.textContent = news.state === 'running' ? t.running :
      news.state === 'done' ? t.imported :
      waitingForFile ? t.safariChoose : fill(t.from, selected.browser);
  if (safari) {
    // What its file has.
    if (news.state === 'found') {
      for (const [item, n] of inOrder(news.counts)) {
        if (n > 0) {
          choicesView.append(pill(`${itemNames[item]} ${numbers.format(n)}`, true, null));
        }
      }
    }
  } else if (news.state !== 'done') {
    for (const item of selected.items) {
      choicesView.append(pill(itemNames[item], !selected.off.has(item), () => {
        selected.off.has(item) ? selected.off.delete(item) : selected.off.add(item);
        render();
      }));
    }
  }
  // Before macOS asks for the user's password, so that it's expected.
  if (selected.asksKeychain && selected.items.includes('passwords') &&
      !selected.off.has('passwords') && !['running', 'done'].includes(news.state)) {
    importNote.textContent = fill(t.keychain, selected.browser);
    importNote.hidden = false;
  }
  if (news.state === 'done') {
    importStatus.textContent = `✓ ${summary(news)}`;
    importStatus.classList.add('good');
  } else if (news.state === 'locked') {
    importStatus.textContent = fill(t.locked, selected.browser);
    importStatus.classList.add('warn');
  } else if (news.state === 'failed' ||
             (news.state === 'found' && !Object.values(news.counts).some((n) => n > 0))) {
    importStatus.textContent = t.failed;
    importStatus.classList.add('warn');
    importButton.disabled = news.state === 'found';
  }
  fit();
}

importButton.addEventListener('click', () => {
  const news = progress.get(selected.id);
  if (selected.family === 'safari') {
    if (news?.state === 'found') {
      chrome.send('importFrom', ['safari', []]);
    } else {
      chrome.send('chooseImportFile', ['safari']);
    }
    return;
  }
  const items = selected.items.filter((item) => !selected.off.has(item));
  if (items.length) {
    chrome.send('importFrom', [selected.id, items]);
  }
});
document.getElementById('import-file').addEventListener('click', () => {
  chrome.send('chooseImportFile', ['bookmarks']);
});

addWebUiListener('import-progress', (news) => {
  progress.set(news.id, news);
  if (news.id === 'bookmarks') {
    // A bookmarks file, not one of the sources.
    fileStatus.hidden = news.state === 'idle';
    fileStatus.className = 'status';
    fileStatus.textContent = news.state === 'running' ? t.running :
        news.state === 'done' ? `✓ ${summary(news)}` : t.failed;
    fileStatus.classList.toggle('good', news.state === 'done');
    fit();
    return;
  }
  render();
});

sendWithPromise('getImportSources').then((list) => {
  sources = list.map((source) => ({...source, off: new Set()}));
  sourcesView.replaceChildren(...sources.map(tile));
  document.getElementById('no-sources').hidden = sources.length > 0;
  // Likely the one the user comes from.
  selected = sources.find((source) => source.isDefault) ?? sources[0] ?? null;
  render();
});

// Nothing scrolls: the step's illustration shrinks to leave room for the rest.
function fit() {
  const root = document.documentElement.style;
  root.setProperty('--k', '1');
  const step = steps[current];
  const art = step.querySelector('.fit');
  if (!art) {
    return;
  }
  const shown = [...step.children].filter((child) => !child.hidden);
  const height = shown.reduce((sum, child) => {
    const style = getComputedStyle(child);
    return sum + child.offsetHeight + parseFloat(style.marginTop) + parseFloat(style.marginBottom);
  }, 0);
  const room = step.clientHeight - 40;
  const k = Math.min(1, (room - (height - art.offsetHeight)) / art.offsetHeight,
                     (innerWidth - 64) / art.offsetWidth);
  root.setProperty('--k', Math.max(0.4, k).toFixed(3));
}
addEventListener('resize', fit);
fit();
// oblada://welcome/#import (the menus' "Import bookmarks and settings") goes to the
// import, as it opens or later.
const showImport = () => show(steps.indexOf(document.getElementById('import')));
addEventListener('hashchange', () => location.hash === '#import' && showImport());
location.hash === '#import' ? showImport() : show(0);
)oblada";

}  // namespace oblada::welcome
