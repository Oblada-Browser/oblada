#include "oblada/browser/containers/containers_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "oblada/browser/containers/containers.h"
#include "oblada/browser/l10n.h"
#include "oblada/browser/new_tab/new_tab_ui.h"
#include "oblada/browser/welcome/welcome_ui.h"
#include "oblada/browser/ui/icons.h"
#include "ui/gfx/color_utils.h"
#include "url/gurl.h"

namespace oblada::containers {

namespace {

constexpr std::string_view kHtml = R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="color-scheme" content="light dark">
  <title>Containers</title>
  <link rel="stylesheet" href="containers.css">
  <script type="module" src="containers.js"></script>
</head>
<body>
  <main>
    <h1></h1>
    <p id="intro"></p>
    <section id="list"></section>
    <section id="add" class="card"></section>
  </main>
</body>
</html>
)";

constexpr std::string_view kCss = R"(
:root {
  --bg: #f4f4f6; --card: #fff; --text: #1c1b22; --muted: #5b5b66; --line: #e0e0e6;
  --accent: #6b5cff; --danger: #d92d20;
  font-family: system-ui, -apple-system, sans-serif;
}
@media (prefers-color-scheme: dark) {
  :root { --bg: #1c1b22; --card: #2b2a33; --text: #fbfbfe; --muted: #a8a8b3;
          --line: #3a3944; --accent: #8f84ff; --danger: #ff6a5c; }
}
body { margin: 0; background: var(--bg); color: var(--text); }
main { max-width: 720px; margin: 48px auto; padding: 0 24px; }
h1 { font-size: 28px; font-weight: 650; margin: 0 0 8px; }
#intro { color: var(--muted); margin: 0 0 28px; line-height: 1.5; }
.card { display: flex; align-items: center; gap: 12px; flex-wrap: wrap;
        background: var(--card); border: 1px solid var(--line); border-radius: 14px;
        padding: 14px 16px; margin-bottom: 10px; }
#add { margin-top: 24px; }
.dot { width: 14px; height: 14px; border-radius: 50%; flex: none; }
input.name { flex: 1; min-width: 140px; font: inherit; color: inherit; background: none;
             border: 1px solid transparent; border-radius: 8px; padding: 6px 8px; }
input.name:hover, input.name:focus { border-color: var(--line); outline: none; }
.colors { display: flex; gap: 6px; }
.swatch { width: 18px; height: 18px; border-radius: 50%; border: 2px solid transparent;
          padding: 0; cursor: pointer; }
.swatch.selected { border-color: var(--text); }
button.action { font: inherit; font-size: 13px; color: var(--text); background: none;
                border: 1px solid var(--line); border-radius: 8px; padding: 6px 10px;
                cursor: pointer; }
button.action:hover { border-color: var(--accent); }
button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
button.danger:hover { border-color: var(--danger); color: var(--danger); }
.note { color: var(--muted); font-size: 13px; }
.sites { flex-basis: 100%; display: flex; align-items: center; gap: 6px; flex-wrap: wrap;
         padding-left: 26px; font-size: 13px; color: var(--muted); }
.site { display: inline-flex; align-items: center; gap: 2px; color: var(--text);
        border: 1px solid var(--line); border-radius: 999px; padding: 2px 4px 2px 10px; }
.site button { font: inherit; color: var(--muted); background: none; border: none;
               cursor: pointer; padding: 0 4px; border-radius: 50%; }
.site button:hover { color: var(--danger); }
input.site-input { font: inherit; color: inherit; background: none; width: 140px;
                   border: 1px dashed var(--line); border-radius: 999px; padding: 3px 10px; }
input.site-input:focus { outline: none; border-color: var(--accent); }
.icon-wrap { position: relative; flex: none; }
.icon-button { width: 32px; height: 32px; border-radius: 8px; border: 1px solid transparent;
               background: none; display: grid; place-items: center; cursor: pointer;
               padding: 0; color: inherit; }
.icon-button:hover { border-color: var(--line); }
.mask { width: 20px; height: 20px; mask-size: contain; mask-repeat: no-repeat;
        mask-position: center; }
.icon-grid { position: absolute; top: 38px; left: 0; z-index: 10; display: grid;
             grid-template-columns: repeat(8, 32px); gap: 2px; padding: 8px;
             background: var(--card); border: 1px solid var(--line); border-radius: 12px;
             box-shadow: 0 10px 30px rgba(0, 0, 0, .28); }
.icon-grid button { width: 32px; height: 32px; border: none; background: none;
                    border-radius: 8px; display: grid; place-items: center;
                    cursor: pointer; padding: 0; }
.icon-grid button:hover { background: color-mix(in srgb, var(--text) 10%, transparent); }
.icon-grid button.selected { background: color-mix(in srgb, var(--text) 20%, transparent); }
.badge { font-size: 12px; color: var(--muted); border: 1px solid var(--line);
         border-radius: 999px; padding: 1px 8px; }
)";

constexpr std::string_view kJs = R"(
import {sendWithPromise} from 'chrome://resources/js/cr.js';

let state;

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, props);
  node.append(...children);
  return node;
}

function button(label, onClick, kind = '') {
  const node = el('button', {className: `action ${kind}`, textContent: label});
  node.addEventListener('click', onClick);
  return node;
}

function colorPicker(selected, onPick) {
  const row = el('div', {className: 'colors'});
  for (const color of state.colors) {
    const swatch = el('button', {
      className: 'swatch' + (color.name === selected ? ' selected' : ''),
      title: color.name,
    });
    swatch.style.background = color.css;
    swatch.addEventListener('click', () => onPick(color.name));
    row.append(swatch);
  }
  return row;
}

// An icon (its mask in `css`), else a dot of it.
function iconOf(id, css) {
  const icon = state.icons.find((i) => i.id === id);
  const node = el('span', {className: icon ? 'mask' : 'dot'});
  node.style.background = css;
  if (icon) node.style.maskImage = `url("${icon.mask}")`;
  return node;
}

// A button showing `selected`, which opens a grid of the icons to pick from.
function iconPicker(selected, css, onPick) {
  const wrap = el('div', {className: 'icon-wrap'});
  const open = el('button', {className: 'icon-button', title: state.strings.icon},
      iconOf(selected, css));
  let grid = null;
  const close = () => {
    grid?.remove();
    grid = null;
    document.removeEventListener('click', outside, true);
  };
  const outside = (e) => wrap.contains(e.target) || close();
  open.addEventListener('click', () => {
    if (grid) return close();
    grid = el('div', {className: 'icon-grid'});
    for (const id of [...state.icons.map((i) => i.id), '']) {
      const option = el('button', {
        className: id === selected ? 'selected' : '',
        title: id ? id.replaceAll('_', ' ') : '—',
      }, iconOf(id, css));
      option.addEventListener('click', () => {
        close();
        onPick(id);
      });
      grid.append(option);
    }
    wrap.append(grid);
    document.addEventListener('click', outside, true);
  });
  wrap.append(open);
  return wrap;
}

async function update(promise) {
  state = await promise;
  render();
}

function render() {
  const s = state.strings;
  document.title = s.title;
  document.documentElement.lang = s.lang;
  document.querySelector('h1').textContent = s.title;
  document.querySelector('#intro').textContent = s.intro;

  const list = document.querySelector('#list');
  list.replaceChildren();
  for (const container of state.containers) {
    const name = el('input', {className: 'name', value: container.name});
    const save = (color, icon = container.icon) => update(sendWithPromise(
        'updateContainer', container.id, name.value, color, icon));
    const icon = iconPicker(container.icon, container.css,
        (picked) => save(container.color, picked));
    name.addEventListener('change', () => save(container.color));
    const note = el('span', {className: 'note'});
    const sites = el('div', {className: 'sites'}, s.sites);
    for (const site of container.sites) {
      const forget = el('button', {textContent: '×', title: s.forget_site});
      forget.addEventListener('click',
          () => update(sendWithPromise('assignSite', site, '')));
      sites.append(el('span', {className: 'site'}, site, forget));
    }
    const siteInput = el('input', {className: 'site-input', placeholder: s.add_site});
    siteInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && siteInput.value.trim()) {
        update(sendWithPromise('assignSite', siteInput.value.trim(), container.id));
      }
    });
    sites.append(siteInput);
    list.append(el('div', {className: 'card'},
        icon, name,
        ...(container.temporary ? [el('span', {className: 'badge'}, s.temporary)] : []),
        colorPicker(container.color, save),
        button(s.open, () => sendWithPromise('openContainer', container.id, '')),
        button(s.clear, async () => {
          if (!confirm(s.clear_confirm)) return;
          await sendWithPromise('clearContainer', container.id);
          note.textContent = s.cleared;
        }),
        button(s.remove, () => {
          if (confirm(s.remove_confirm)) {
            update(sendWithPromise('removeContainer', container.id));
          }
        }, 'danger'),
        note,
        ...(container.temporary ? [] : [sites])));
  }
  if (state.containers.some((container) => container.temporary)) {
    list.append(el('p', {className: 'note'}, s.temporary_note));
  }

  const add = document.querySelector('#add');
  let color = state.colors[0].name;
  let icon = '';
  const name = el('input', {className: 'name', placeholder: s.name});
  const css = () => state.colors.find((c) => c.name === color).css;
  const iconSlot = el('div', {});
  const iconChoice = () => iconSlot.replaceChildren(iconPicker(icon, css(), (picked) => {
    icon = picked;
    iconChoice();
  }));
  iconChoice();
  const picker = () => colorPicker(color, (picked) => {
    color = picked;
    pickerSlot.replaceChildren(picker());
    iconChoice();
  });
  const pickerSlot = el('div', {}, picker());
  const create = () => {
    if (name.value.trim()) {
      update(sendWithPromise('addContainer', name.value.trim(), color, icon));
    }
  };
  name.addEventListener('keydown', (e) => e.key === 'Enter' && create());
  add.replaceChildren(iconSlot, name, pickerSlot, button(s.add, create, 'primary'),
      button(s.new_temporary, () => sendWithPromise('openTemporary', '')));
}

update(sendWithPromise('getState'));
)";

class ContainersHandler : public content::WebUIMessageHandler {
 public:
  ContainersHandler() = default;
  ContainersHandler(const ContainersHandler&) = delete;
  ContainersHandler& operator=(const ContainersHandler&) = delete;
  ~ContainersHandler() override = default;

  void RegisterMessages() override {
    Register("getState", &ContainersHandler::HandleGetState);
    Register("addContainer", &ContainersHandler::HandleAdd);
    Register("updateContainer", &ContainersHandler::HandleUpdate);
    Register("removeContainer", &ContainersHandler::HandleRemove);
    Register("clearContainer", &ContainersHandler::HandleClear);
    Register("openContainer", &ContainersHandler::HandleOpen);
    Register("assignSite", &ContainersHandler::HandleAssignSite);
    Register("openTemporary", &ContainersHandler::HandleOpenTemporary);
  }

 private:
  void Register(std::string_view message,
                void (ContainersHandler::*handler)(const base::ListValue&)) {
    web_ui()->RegisterMessageCallback(
        message, base::BindRepeating(handler, base::Unretained(this)));
  }

  Profile* profile() { return Profile::FromWebUI(web_ui()); }

  static std::string Arg(const base::ListValue& args, size_t index) {
    const std::string* value =
        index < args.size() ? args[index].GetIfString() : nullptr;
    return value ? *value : std::string();
  }

  // The icons to pick from, as masks for the page.
  static const base::ListValue& Icons() {
    static const base::NoDestructor<base::ListValue> icons([] {
      base::ListValue list;
      for (const icons::Icon& icon : icons::All()) {
        list.Append(base::DictValue()
                        .Set("id", icon.id)
                        .Set("mask", icons::MaskUrl(icon.id, 20)));
      }
      return list;
    }());
    return *icons;
  }

  // One of them, else a dot (as Firefox calls it).
  static std::string ValidIcon(const std::string& icon) {
    return icons::Find(icon) ? std::string(icons::Canonical(icon)) : "circle";
  }

  static std::string ValidColor(const std::string& color) {
    for (std::string_view name : ColorNames()) {
      if (name == color) {
        return color;
      }
    }
    return std::string(ColorNames().front());
  }

  base::DictValue State() {
    base::DictValue strings;
    for (const char* key : {"title", "intro", "add", "name", "color", "icon", "open",
                            "clear", "clear_confirm", "cleared", "remove",
                            "remove_confirm", "save", "sites", "add_site",
                            "forget_site", "temporary", "temporary_note",
                            "new_temporary"}) {
      strings.Set(key, L10nUTF8(std::string("containers.") + key));
    }
    strings.Set("lang", L10nUTF8("containers.title") == "Container" ? "it" : "en");

    base::ListValue colors;
    for (std::string_view name : ColorNames()) {
      colors.Append(base::DictValue().Set("name", name).Set(
          "css", color_utils::SkColorToRgbaString(ColorFor(name))));
    }
    base::ListValue containers;
    for (const Container& container : GetContainers(profile())) {
      base::ListValue sites;
      for (const std::string& site : AssignedSites(profile(), container.id)) {
        sites.Append(site);
      }
      containers.Append(
          base::DictValue()
              .Set("id", container.id)
              .Set("name", container.name)
              .Set("color", container.color)
              .Set("icon", icons::Canonical(container.icon))
              .Set("temporary", container.temporary)
              .Set("sites", std::move(sites))
              .Set("css", color_utils::SkColorToRgbaString(ColorFor(container.color))));
    }
    return base::DictValue()
        .Set("strings", std::move(strings))
        .Set("colors", std::move(colors))
        .Set("icons", Icons().Clone())
        .Set("containers", std::move(containers));
  }

  void Reply(const base::ListValue& args, base::ValueView result) {
    AllowJavascript();
    ResolveJavascriptCallback(args[0], result);
  }

  void HandleGetState(const base::ListValue& args) { Reply(args, State()); }

  void HandleAdd(const base::ListValue& args) {
    const std::string name(base::TrimWhitespaceASCII(Arg(args, 1), base::TRIM_ALL));
    if (!name.empty()) {
      AddContainer(profile(), name, ValidColor(Arg(args, 2)),
                   ValidIcon(Arg(args, 3)));
    }
    Reply(args, State());
  }

  void HandleUpdate(const base::ListValue& args) {
    std::optional<Container> container = FindContainer(profile(), Arg(args, 1));
    const std::string name(base::TrimWhitespaceASCII(Arg(args, 2), base::TRIM_ALL));
    if (container && !name.empty()) {
      container->name = name;
      container->color = ValidColor(Arg(args, 3));
      container->icon = ValidIcon(Arg(args, 4));
      UpdateContainer(profile(), *container);
    }
    Reply(args, State());
  }

  void HandleRemove(const base::ListValue& args) {
    if (FindContainer(profile(), Arg(args, 1))) {
      RemoveContainer(profile(), Arg(args, 1));
    }
    Reply(args, State());
  }

  void HandleClear(const base::ListValue& args) {
    if (!FindContainer(profile(), Arg(args, 1))) {
      Reply(args, base::Value(false));
      return;
    }
    AllowJavascript();
    ClearContainerData(profile(), Arg(args, 1),
                       base::BindOnce(&ContainersHandler::OnCleared,
                                      weak_factory_.GetWeakPtr(), args[0].Clone()));
  }

  void OnCleared(base::Value callback_id) {
    ResolveJavascriptCallback(callback_id, base::Value(true));
  }

  // Opens a tab in the container, on `url` if it is a web page (the New Tab page if
  // empty), in the window this page is in.
  void HandleOpen(const base::ListValue& args) {
    const GURL url(Arg(args, 2));
    tabs::TabInterface* tab =
        tabs::TabInterface::MaybeGetFromContents(web_ui()->GetWebContents());
    const bool ok = tab && FindContainer(profile(), Arg(args, 1)) &&
                    (url.is_empty() || url.SchemeIsHTTPOrHTTPS());
    if (ok) {
      OpenInContainer(tab->GetBrowserWindowInterface(), Arg(args, 1), url,
                      WindowOpenDisposition::NEW_FOREGROUND_TAB);
    }
    Reply(args, base::Value(ok));
  }

  // Opens a tab in a new temporary container, on `url` as for HandleOpen.
  void HandleOpenTemporary(const base::ListValue& args) {
    const GURL url(Arg(args, 1));
    tabs::TabInterface* tab =
        tabs::TabInterface::MaybeGetFromContents(web_ui()->GetWebContents());
    const bool ok = tab && (url.is_empty() || url.SchemeIsHTTPOrHTTPS());
    if (ok) {
      OpenInContainer(tab->GetBrowserWindowInterface(),
                      AddTemporaryContainer(profile()).id, url,
                      WindowOpenDisposition::NEW_FOREGROUND_TAB);
    }
    Reply(args, base::Value(ok));
  }

  // Always opens a site (typed as a host or a URL) in a container; no container
  // forgets it.
  void HandleAssignSite(const base::ListValue& args) {
    std::string site = Arg(args, 1);
    if (site.find("://") == std::string::npos) {
      site = "https://" + site;
    }
    site = SiteOf(GURL(site));
    const std::string id = Arg(args, 2);
    if (!site.empty() && (id.empty() || FindContainer(profile(), id))) {
      AssignSite(profile(), site, id);
    }
    Reply(args, State());
  }

  base::WeakPtrFactory<ContainersHandler> weak_factory_{this};
};

}  // namespace

std::unique_ptr<content::WebUIMessageHandler> CreateContainersHandler() {
  return std::make_unique<ContainersHandler>();
}

ContainersUI::ContainersUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  web_ui->AddMessageHandler(CreateContainersHandler());

  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kContainersHost);
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) {
        return path.empty() || path == "containers.js" || path == "containers.css";
      }),
      base::BindRepeating([](const std::string& path,
                             content::WebUIDataSource::GotDataCallback done) {
        const std::string_view data = path == "containers.js"    ? kJs
                                      : path == "containers.css" ? kCss
                                                                 : kHtml;
        std::move(done).Run(
            base::MakeRefCounted<base::RefCountedString>(std::string(data)));
      }));
}

ContainersUI::~ContainersUI() = default;

ContainersUIConfig::ContainersUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kContainersHost) {}

}  // namespace oblada::containers

namespace oblada {

void RegisterWebUIConfigs(content::WebUIConfigMap& map) {
  map.AddWebUIConfig(std::make_unique<containers::ContainersUIConfig>());
  map.AddWebUIConfig(std::make_unique<new_tab::NewTabUIConfig>());
  map.AddWebUIConfig(std::make_unique<welcome::WelcomeUIConfig>());
}

}  // namespace oblada
