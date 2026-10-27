// The Barrier's cosmetic filters on a page (../barrier_agent.h), in an isolated
// world of their own: the procedural filters (uBlock Origin's `:has-text()`,
// `:upward()`, `:xpath()`… and actions, `:remove()`, `:style()`…), and the
// generic rules (`##.ad`) the page's classes and ids call for, which the
// engines know. Out of aggressive blocking, generic rules leave the page's own
// content alone, hiding only what holds third parties' (as Brave does).
//
// `obladaBarrier`, from the agent:
// - config: {aggressive, genericHide, procedural: [JSON…]};
// - hiddenSelectors(classes, ids, callback): the generic rules for them;
// - hide(selectors): hides what they select, with a user style sheet;
// - sameSite(url): whether `url` is the page's site.
(() => {
  const barrier = obladaBarrier;
  const {aggressive, genericHide, procedural} = barrier.config;

  // --- Procedural filters ---

  // A pattern of uBlock Origin's: /regex/flags, or plain text.
  const toRegex = (pattern) => {
    const match = /^\/(.+)\/([imu]*)$/.exec(pattern);
    if (match) {
      try {
        return new RegExp(match[1], match[2]);
      } catch {
        return null;
      }
    }
    return new RegExp(pattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
  };

  const unquote = (text) => text.replace(/^(["'])(.*)\1$/, '$2');

  const siblingsAfter = (element, next) => {
    const result = [];
    for (let e = element.nextElementSibling; e; e = e.nextElementSibling) {
      result.push(e);
      if (next) {
        break;
      }
    }
    return result;
  };

  // Each operator, from the elements so far to the next ones.
  const OPERATORS = {
    'css-selector': (elements, arg, first) => {
      if (first) {
        return [...document.querySelectorAll(arg)];
      }
      const trimmed = arg.trimStart();
      if (trimmed.startsWith('+') || trimmed.startsWith('~')) {
        const selector = trimmed.slice(1).trim();
        return elements.flatMap((e) => siblingsAfter(e, trimmed[0] === '+'))
            .filter((e) => e.matches(selector));
      }
      if (trimmed.startsWith('>') || trimmed !== arg) {
        return elements.flatMap((e) => [...e.querySelectorAll(`:scope ${arg}`)]);
      }
      return elements.filter((e) => e.matches(arg));
    },
    'has-text': (elements, arg) => {
      const re = toRegex(arg);
      return re ? elements.filter((e) => re.test(e.textContent)) : [];
    },
    'min-text-length': (elements, arg) =>
        elements.filter((e) => e.textContent.length >= Number(arg)),
    'matches-attr': (elements, arg) => {
      const match = /^\s*(.+?)\s*(?:=\s*(.+?))?\s*$/.exec(arg);
      if (!match) {
        return [];
      }
      const name = toRegex(unquote(match[1]));
      const value = match[2] === undefined ? null : toRegex(unquote(match[2]));
      return name ? elements.filter((e) => [...e.attributes].some((a) =>
          name.test(a.name) && (!value || value.test(a.value)))) : [];
    },
    'matches-css': (elements, arg) => matchesCss(elements, arg, null),
    'matches-css-before': (elements, arg) => matchesCss(elements, arg, '::before'),
    'matches-css-after': (elements, arg) => matchesCss(elements, arg, '::after'),
    'matches-path': (elements, arg) => {
      const re = toRegex(arg);
      return re && re.test(location.pathname + location.search) ? elements : [];
    },
    'upward': (elements, arg) => {
      const n = Number(arg);
      return elements.map((e) => {
        if (Number.isInteger(n) && n > 0) {
          let ancestor = e;
          for (let i = 0; i < n && ancestor; i++) {
            ancestor = ancestor.parentElement;
          }
          return ancestor;
        }
        return e.parentElement?.closest(arg);
      }).filter(Boolean);
    },
    'xpath': (elements, arg, first) => {
      const result = [];
      for (const context of first ? [document] : elements) {
        const snapshot = document.evaluate(
            arg, context, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        for (let i = 0; i < snapshot.snapshotLength; i++) {
          const node = snapshot.snapshotItem(i);
          if (node instanceof Element) {
            result.push(node);
          }
        }
      }
      return result;
    },
  };

  function matchesCss(elements, arg, pseudo) {
    const colon = arg.indexOf(':');
    if (colon < 0) {
      return [];
    }
    const property = arg.slice(0, colon).trim();
    const value = toRegex(arg.slice(colon + 1).trim());
    return value ? elements.filter((e) =>
        value.test(getComputedStyle(e, pseudo).getPropertyValue(property))) :
        [];
  }

  const filters = procedural.map((json) => {
    try {
      return JSON.parse(json);
    } catch {
      return null;
    }
  }).filter((filter) => filter && filter.selector?.length);

  // Elements a filter is done with (hidden, removed…), not done again.
  const done = new WeakMap();

  function applyFilter(filter) {
    let elements = [];
    try {
      filter.selector.forEach(({type, arg}, i) => {
        const operator = OPERATORS[type];
        elements = operator ? [...new Set(operator(elements, arg, i === 0))] : [];
      });
    } catch {
      return;
    }
    let seen = done.get(filter);
    if (!seen) {
      seen = new WeakSet();
      done.set(filter, seen);
    }
    const {type, arg} = filter.action ?? {};
    for (const element of elements) {
      if (seen.has(element)) {
        continue;
      }
      seen.add(element);
      switch (type) {
        case 'remove':
          element.remove();
          break;
        case 'style':
          if (!/[{}]|url\(|expression|@import/i.test(arg)) {
            for (const declaration of arg.split(';')) {
              const colon = declaration.indexOf(':');
              if (colon > 0) {
                const value = declaration.slice(colon + 1).replace(
                    /!\s*important\s*$/i, '').trim();
                element.style.setProperty(
                    declaration.slice(0, colon).trim(), value, 'important');
              }
            }
          }
          break;
        case 'remove-attr':
          element.removeAttribute(arg);
          break;
        case 'remove-class':
          element.classList.remove(arg);
          break;
        default:
          element.style.setProperty('display', 'none', 'important');
      }
    }
  }

  // --- Generic rules ---

  const seenClasses = new Set();
  const seenIds = new Set();
  let newClasses = [];
  let newIds = [];

  function collect(element) {
    if (element.id && !seenIds.has(element.id)) {
      seenIds.add(element.id);
      newIds.push(element.id);
    }
    for (const name of element.classList ?? []) {
      if (!seenClasses.has(name)) {
        seenClasses.add(name);
        newClasses.push(name);
      }
    }
  }

  // Whether `element` is the page's own content rather than a third party's:
  // nothing in it comes from another site, and it has text.
  function isFirstParty(element) {
    for (const e of [element, ...element.querySelectorAll(
        'iframe, img, video, audio, source, embed, object, script, a[href]')]) {
      const url = e.src || e.href || e.data;
      if (typeof url === 'string' && /^https?:/.test(url) &&
          !barrier.sameSite(url)) {
        return false;
      }
    }
    return element.textContent.trim().length > 0;
  }

  function onSelectors(selectors) {
    const hidden = selectors.filter((selector) => {
      if (aggressive) {
        return true;
      }
      try {
        return ![...document.querySelectorAll(selector)].some(isFirstParty);
      } catch {
        return false;
      }
    });
    if (hidden.length) {
      barrier.hide(hidden);
    }
  }

  function sendNew() {
    if (genericHide || (!newClasses.length && !newIds.length)) {
      return;
    }
    const classes = newClasses;
    const ids = newIds;
    newClasses = [];
    newIds = [];
    barrier.hiddenSelectors(classes, ids, onSelectors);
  }

  // --- As the page changes ---

  let pending = false;
  const added = [];

  function run() {
    pending = false;
    for (const node of added.splice(0)) {
      if (node.isConnected) {
        collect(node);
        node.querySelectorAll?.('[id], [class]').forEach(collect);
      }
    }
    sendNew();
    filters.forEach(applyFilter);
  }

  function schedule() {
    if (!pending) {
      pending = true;
      setTimeout(run, 50);
    }
  }

  function start() {
    if (document.documentElement) {
      added.push(document.documentElement);
    }
    run();
    new MutationObserver((mutations) => {
      for (const mutation of mutations) {
        if (mutation.type === 'attributes') {
          added.push(mutation.target);
        }
        for (const node of mutation.addedNodes) {
          if (node instanceof Element) {
            added.push(node);
          }
        }
      }
      schedule();
    }).observe(document, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ['class', 'id'],
    });
  }

  // From the start, as the parser adds elements; and once more when it's
  // done, for what the filters select by the whole page.
  start();
  document.addEventListener('DOMContentLoaded', schedule, {once: true});
})();
