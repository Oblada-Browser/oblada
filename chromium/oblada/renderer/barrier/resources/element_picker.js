// The Barrier's element picker (../barrier_agent.h), in the cosmetic filters'
// isolated world: the element under the pointer is highlighted; clicking picks
// it, and a panel shows the rule that hides it (a CSS selector, which can be
// made wider or narrower, or edited), and how many elements it hides. "Block"
// hides them and keeps the rule among the user's own filters
// (`host##selector`).
//
// `obladaBarrier`, from the agent: pickerStrings() ({hint, block, cancel,
// wider, narrower, count}), hide(selectors), addFilter(rule).
(() => {
  const barrier = obladaBarrier;
  const strings = barrier.pickerStrings();
  if (document.querySelector('oblada-element-picker')) {
    return;
  }

  const host = document.createElement('oblada-element-picker');
  host.style.cssText = 'all: initial; position: fixed; inset: 0; ' +
      'z-index: 2147483647; display: block;';
  const root = host.attachShadow({mode: 'closed'});
  root.innerHTML = `
    <style>
      :host { all: initial; }
      .overlay { position: fixed; inset: 0; cursor: crosshair; }
      .box {
        position: fixed; pointer-events: none; display: none;
        background: rgba(58, 130, 247, 0.25);
        border: 2px solid rgb(58, 130, 247); border-radius: 3px;
      }
      .panel {
        position: fixed; left: 50%; bottom: 24px; transform: translateX(-50%);
        width: min(560px, calc(100vw - 48px)); box-sizing: border-box;
        background: #1f2023; color: #e8eaed; border-radius: 14px;
        box-shadow: 0 8px 28px rgba(0, 0, 0, 0.35); padding: 14px;
        font: 13px system-ui, sans-serif; display: flex; flex-direction: column;
        gap: 10px;
      }
      .hint { opacity: 0.8; }
      input {
        font: 12px ui-monospace, monospace; background: #2d2e31;
        color: inherit; border: 1px solid #44464a; border-radius: 8px;
        padding: 8px; width: 100%; box-sizing: border-box;
      }
      .row { display: flex; gap: 8px; align-items: center; }
      .count { flex: 1; opacity: 0.8; }
      button {
        font: inherit; border: none; border-radius: 8px; padding: 7px 12px;
        background: #3c3d41; color: inherit; cursor: pointer;
      }
      button.block { background: #8ab4f8; color: #1f2023; font-weight: 600; }
      button:disabled { opacity: 0.4; cursor: default; }
    </style>
    <div class="overlay"></div>
    <div class="box"></div>
    <div class="panel">
      <div class="hint"></div>
      <input type="text" spellcheck="false" hidden>
      <div class="row">
        <span class="count"></span>
        <button class="narrower" hidden></button>
        <button class="wider" hidden></button>
        <button class="cancel"></button>
        <button class="block" disabled></button>
      </div>
    </div>`;
  const $ = (selector) => root.querySelector(selector);
  const overlay = $('.overlay');
  const box = $('.box');
  const input = $('input');
  $('.hint').textContent = strings.hint;
  $('.block').textContent = strings.block;
  $('.cancel').textContent = strings.cancel;
  $('.wider').textContent = strings.wider;
  $('.narrower').textContent = strings.narrower;

  // The elements picked: the one clicked, then its ancestors as made wider.
  let chain = [];
  let level = 0;

  const elementAt = (x, y) => document.elementsFromPoint(x, y)
      .find((e) => e !== host && !host.contains(e) &&
              e !== document.documentElement && e !== document.body);

  function highlight(element) {
    if (!element) {
      box.style.display = 'none';
      return;
    }
    const rect = element.getBoundingClientRect();
    Object.assign(box.style, {
      display: 'block', left: `${rect.left}px`, top: `${rect.top}px`,
      width: `${rect.width}px`, height: `${rect.height}px`,
    });
  }

  // A selector for `element`: by its id if it has a steady one, else by its
  // tag and classes, under its parents' as far as needed to be specific.
  const steady = (name) => name && !/\d{3,}|[0-9a-f]{8,}/i.test(name);
  function part(element) {
    if (steady(element.id)) {
      return `#${CSS.escape(element.id)}`;
    }
    let selector = element.localName;
    const classes = [...element.classList].filter(steady).slice(0, 3);
    selector += classes.map((c) => `.${CSS.escape(c)}`).join('');
    const parent = element.parentElement;
    if (parent && classes.length === 0) {
      const same = [...parent.children].filter(
          (e) => e.localName === element.localName);
      if (same.length > 1) {
        selector += `:nth-of-type(${same.indexOf(element) + 1})`;
      }
    }
    return selector;
  }
  function selectorFor(element) {
    const parts = [];
    for (let e = element; e && e !== document.body &&
         e !== document.documentElement; e = e.parentElement) {
      parts.unshift(part(e));
      const selector = parts.join(' > ');
      if (parts[0].startsWith('#') || document.querySelectorAll(selector).length <= 1) {
        return selector;
      }
      if (parts.length >= 4) {
        return selector;
      }
    }
    return parts.join(' > ');
  }

  function count(selector) {
    try {
      return selector.trim() ? document.querySelectorAll(selector).length : 0;
    } catch {
      return 0;
    }
  }

  function update() {
    const n = count(input.value);
    $('.count').textContent = strings.count.replace('$1', n);
    $('.block').disabled = n === 0;
    try {
      highlight(n ? document.querySelector(input.value) : null);
    } catch {
      highlight(null);
    }
  }

  function pick() {
    input.value = selectorFor(chain[level]);
    input.hidden = false;
    $('.wider').hidden = !chain[level].parentElement ||
        chain[level].parentElement === document.body;
    $('.narrower').hidden = level === 0;
    update();
  }

  function close() {
    host.remove();
    document.removeEventListener('keydown', onKey, true);
  }

  function onKey(event) {
    if (event.key === 'Escape') {
      event.stopPropagation();
      close();
    }
  }

  overlay.addEventListener('mousemove', (event) => {
    if (!chain.length) {
      highlight(elementAt(event.clientX, event.clientY));
    }
  });
  overlay.addEventListener('click', (event) => {
    const element = elementAt(event.clientX, event.clientY);
    if (element) {
      chain = [element];
      level = 0;
      overlay.style.cursor = 'default';
      pick();
    }
  });
  $('.wider').addEventListener('click', () => {
    if (level === chain.length - 1) {
      chain.push(chain[level].parentElement);
    }
    level++;
    pick();
  });
  $('.narrower').addEventListener('click', () => {
    level--;
    pick();
  });
  input.addEventListener('input', update);
  $('.cancel').addEventListener('click', close);
  $('.block').addEventListener('click', () => {
    const selector = input.value.trim();
    if (!count(selector) || /[{}]/.test(selector)) {
      return;
    }
    barrier.hide([selector]);
    barrier.addFilter(`${location.hostname}##${selector}`);
    close();
  });
  document.addEventListener('keydown', onKey, true);
  document.documentElement.appendChild(host);
})();
