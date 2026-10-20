#include "oblada/browser/l10n.h"

#include <array>

#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"

namespace oblada {

namespace {

struct Entry {
  std::string_view key;
  std::string_view en;
  std::string_view it;
};

constexpr auto kStrings = std::to_array<Entry>({
    {"containers.title", "Containers", "Container"},
    {"containers.new_tab", "New container tab", "Nuova scheda container"},
    {"containers.open_link", "Open link in new container tab",
     "Apri link in una nuova scheda container"},
    {"containers.manage", "Manage containers…", "Gestisci container…"},
    {"containers.none", "No container", "Nessun container"},
    {"containers.personal", "Personal", "Personale"},
    {"containers.work", "Work", "Lavoro"},
    {"containers.banking", "Banking", "Banca"},
    {"containers.shopping", "Shopping", "Acquisti"},
    {"containers.intro",
     "Each container keeps its own cookies, logins and site data: open the same site "
     "in two containers to use two accounts side by side.",
     "Ogni container ha cookie, accessi e dati dei siti tutti suoi: apri lo stesso sito "
     "in due container per usare due account fianco a fianco."},
    {"containers.add", "New container", "Nuovo container"},
    {"containers.name", "Name", "Nome"},
    {"containers.color", "Color", "Colore"},
    {"containers.icon", "Icon", "Icona"},
    {"containers.open", "Open tab", "Apri scheda"},
    {"containers.clear", "Clear data", "Cancella dati"},
    {"containers.clear_confirm",
     "Delete all cookies and site data of this container?",
     "Eliminare tutti i cookie e i dati dei siti di questo container?"},
    {"containers.cleared", "Data cleared", "Dati cancellati"},
    {"containers.remove", "Delete", "Elimina"},
    {"containers.remove_confirm",
     "Delete this container? Its tabs are closed and its data is erased.",
     "Eliminare questo container? Le sue schede vengono chiuse e i suoi dati cancellati."},
    {"containers.save", "Save", "Salva"},
    {"containers.temporary", "Temporary", "Temporaneo"},
    {"containers.new_temporary", "New temporary container",
     "Nuovo container temporaneo"},
    {"containers.temporary_note",
     "Temporary containers keep nothing on disk and are deleted, with their data, "
     "when their last tab is closed.",
     "I container temporanei non salvano niente sul disco e vengono eliminati, con i "
     "loro dati, quando chiudi la loro ultima scheda."},
    {"containers.reopen", "Reopen in container", "Riapri in un container"},
    {"containers.always_open", "Always open $1 in this container",
     "Apri sempre $1 in questo container"},
    {"containers.sites", "Always opened here:", "Si aprono sempre qui:"},
    {"containers.add_site", "Add site", "Aggiungi sito"},
    {"containers.forget_site", "Stop opening it here", "Non aprirlo più qui"},
    {"new_tab.title", "New Tab", "Nuova scheda"},
    {"peek.close", "Close", "Chiudi"},
    {"peek.open_as_tab", "Open as a tab", "Apri come scheda"},
    {"peek.open_side_by_side", "Open side by side", "Apri affiancata"},
    {"sidebar.compact", "Compact mode", "Modalità compatta"},
    {"welcome.lang", "en", "it"},
    {"welcome.title", "Welcome to Oblada", "Benvenuto in Oblada"},
    {"welcome.lead",
     "A calm browser: Zen's sidebar, Firefox's containers, and extensions from "
     "both Chrome and Firefox.",
     "Un browser calmo: la sidebar di Zen, i container di Firefox e le estensioni "
     "sia di Chrome sia di Firefox."},
    {"welcome.start", "Get started", "Iniziamo"},
    {"welcome.skip", "Skip", "Salta"},
    {"welcome.back", "Back", "Indietro"},
    {"welcome.next", "Next", "Avanti"},
    {"welcome.finish", "Start browsing", "Inizia a navigare"},
    {"welcome.import_title", "Bring your things along",
     "Porta con te le tue cose"},
    {"welcome.import_text",
     "Bookmarks, history and passwords from the browser you used until now.",
     "Preferiti, cronologia e password dal browser che usavi finora."},
    {"welcome.import_from", "Import from $1", "Importa da $1"},
    {"welcome.import_running", "Importing…", "Importazione in corso…"},
    {"welcome.import_done", "Imported", "Importato"},
    {"welcome.import_bookmarks", "Bookmarks", "Preferiti"},
    {"welcome.import_history", "History", "Cronologia"},
    {"welcome.import_passwords", "Passwords", "Password"},
    {"welcome.import_autofill", "Form data", "Dati dei moduli"},
    {"welcome.import_cards", "Payment cards", "Carte di pagamento"},
    {"welcome.import_safari_how",
     "Safari keeps its data to itself: in Safari, choose File › Export "
     "Browsing Data to File…, then pick that file here.",
     "Safari tiene i suoi dati per sé: in Safari scegli Archivio › Esporta dati "
     "di navigazione in un file…, poi scegli qui quel file."},
    {"welcome.import_safari_choose", "Choose the exported file…",
     "Scegli il file esportato…"},
    {"welcome.import_locked",
     "Close $1 first: it keeps its data locked while it is open.",
     "Prima chiudi $1: finché è aperto, i suoi dati restano bloccati."},
    {"welcome.import_failed", "Nothing could be imported.",
     "Non è stato possibile importare nulla."},
    {"welcome.import_keychain",
     "macOS will ask for your Mac's password: $1 keeps its passwords locked in "
     "the Keychain.",
     "macOS ti chiederà la password del Mac: $1 tiene le sue password chiuse nel "
     "Portachiavi."},
    {"welcome.import_unreadable", "passwords not read: $1",
     "password non lette: $1"},
    {"welcome.import_none", "No other browser found on this computer.",
     "Nessun altro browser trovato su questo computer."},
    {"welcome.import_file", "Or import a bookmarks file (HTML)…",
     "Oppure importa un file di preferiti (HTML)…"},
    {"import.folder", "Imported from $1", "Importati da $1"},
    {"import.container_personal", "Personal", "Personale"},
    {"import.container_work", "Work", "Lavoro"},
    {"import.container_banking", "Banking", "Banca"},
    {"import.container_shopping", "Shopping", "Acquisti"},
    {"welcome.containers_title", "Your containers", "I tuoi container"},
    {"welcome.containers_setup_text",
     "Each keeps its own cookies and logins: one site, different accounts. "
     "Make them yours; you can change them whenever you like.",
     "Ognuno tiene i suoi cookie e i suoi accessi: lo stesso sito, account "
     "diversi. Sistemali come preferisci; potrai cambiarli quando vuoi."},
    {"welcome.containers_name", "Name", "Nome"},
    {"welcome.containers_sites", "Always opened here:", "Si aprono sempre qui:"},
    {"welcome.containers_add_site", "Add a site, e.g. github.com",
     "Aggiungi un sito, es. github.com"},
    {"welcome.containers_remove", "Delete", "Elimina"},
    {"welcome.containers_done", "Done", "Fatto"},
    {"welcome.containers_new", "New", "Nuovo"},
    {"welcome.containers_add", "Add", "Aggiungi"},
    {"welcome.containers_import_from", "Import the containers from $1",
     "Importa i container da $1"},
    {"welcome.containers_imported", "Containers imported: $1",
     "Container importati: $1"},
    {"welcome.default_title", "Open links in Oblada", "Apri i link in Oblada"},
    {"welcome.default_text",
     "Links from your mail, your chats and every other app will open here.",
     "I link della posta, delle chat e di ogni altra app si apriranno qui."},
    {"welcome.default", "Make Oblada your default browser",
     "Rendi Oblada il browser predefinito"},
    {"welcome.default_done", "Oblada is your default browser",
     "Oblada è il tuo browser predefinito"},
    {"welcome.workspaces", "Workspaces", "Workspace"},
    {"welcome.workspaces_text",
     "Separate spaces for home, work and everything else, each with its own tabs "
     "and its own color.",
     "Spazi separati per casa, lavoro e tutto il resto, ognuno con le sue schede "
     "e il suo colore."},
    {"welcome.workspaces_hint", "Swipe across the sidebar with two fingers",
     "Scorri con due dita sulla sidebar"},
    {"welcome.containers", "Containers", "Container"},
    {"welcome.containers_text",
     "Cookies and logins kept apart in the same window: two accounts on one site, "
     "side by side. A site can always open in its own container.",
     "Cookie e accessi tenuti separati nella stessa finestra: due account sullo "
     "stesso sito, uno accanto all'altro. Un sito può aprirsi sempre nel suo "
     "container."},
    {"welcome.containers_hint", "Set up your containers", "Prepara i tuoi container"},
    {"welcome.work", "Work", "Lavoro"},
    {"welcome.personal", "Personal", "Personale"},
    {"welcome.address_bar", "The address bar", "La barra degli indirizzi"},
    {"welcome.address_bar_text",
     "It opens in the middle of the page when you need it, and stays out of your "
     "way the rest of the time.",
     "Si apre al centro della pagina quando ti serve, e il resto del tempo non ti "
     "è d'intralcio."},
#if BUILDFLAG(IS_MAC)
    {"welcome.address_bar_hint", "Press ⌘L or ⌘T", "Premi ⌘L o ⌘T"},
#else
    {"welcome.address_bar_hint", "Press Ctrl+L or Ctrl+T", "Premi Ctrl+L o Ctrl+T"},
#endif
    {"welcome.address_query", "ferries to Procida", "traghetti per Procida"},
    {"welcome.peek", "Peek", "Peek"},
    {"welcome.peek_text",
     "Look at a link without leaving the page, and keep it as a tab if you like.",
     "Guarda un link senza lasciare la pagina, e tienilo come scheda se ti serve."},
#if BUILDFLAG(IS_MAC)
    {"welcome.peek_hint", "Option-click or force-click a link",
     "Option-clic o force click su un link"},
#else
    {"welcome.peek_hint", "Alt-click a link", "Alt+clic su un link"},
#endif
    {"welcome.peek_link", "Ferry timetables", "Orari dei traghetti"},
    {"welcome.split", "Split view", "Split view"},
    {"welcome.split_text", "Two tabs side by side, in the same window.",
     "Due schede affiancate, nella stessa finestra."},
    {"welcome.split_hint", "Drag a tab onto the page",
     "Trascina una scheda sulla pagina"},
    {"welcome.compact", "Compact mode", "Modalità compatta"},
    {"welcome.compact_text",
     "The sidebar folds away to give the page all the room, and comes back when "
     "you point at the window's edge.",
     "La sidebar si ripiega per lasciare tutto lo spazio alla pagina, e torna "
     "quando punti al bordo della finestra."},
    {"welcome.compact_hint", "In the ⋯ menu: Compact mode",
     "Nel menu ⋯: Modalità compatta"},
    {"welcome.extensions", "Extensions", "Estensioni"},
    {"welcome.extensions_text",
     "Install them from the Chrome Web Store or from Firefox Add-ons: Firefox's "
     "are converted as they install.",
     "Installale dal Chrome Web Store o da Firefox Add-ons: quelle di Firefox "
     "vengono convertite durante l'installazione."},
    {"welcome.done_title", "All set", "Tutto pronto"},
    {"welcome.zen", "Oblada's calm comes from Zen. If you like it,",
     "La calma di Oblada viene da Zen. Se ti piace,"},
    {"welcome.zen_link", "support Zen.", "sostieni Zen."},
    {"workspaces.home", "Home", "Home"},
    {"workspaces.new", "Space $1", "Spazio $1"},
    {"workspaces.add", "New workspace", "Nuovo workspace"},
    {"workspaces.move_to", "Move to workspace", "Sposta nel workspace"},
    {"workspaces.rename", "Rename…", "Rinomina…"},
    {"workspaces.icon_color", "Icon and color…", "Icona e colore…"},
    {"workspaces.tints_window", "Color the window", "Colora la finestra"},
    {"workspaces.container", "New tabs in container", "Nuove schede nel container"},
    {"workspaces.no_container", "No container", "Nessun container"},
    {"workspaces.delete", "Delete workspace", "Elimina workspace"},
    {"split.handle",
     "Drag onto the other side to swap, or onto the sidebar to separate. Click for "
     "more.",
     "Trascina sull'altro lato per scambiare, o sulla barra laterale per separare. "
     "Clic per altro."},
    {"split.swap", "Swap sides", "Scambia i lati"},
    {"split.separate", "Separate into whole pages", "Separa in pagine intere"},
    {"split.close", "Close tab", "Chiudi la scheda"},
    {"about.version", "Version $1, based on Chromium $2 ($3)",
     "Versione $1, basata su Chromium $2 ($3)"},
    {"about.copyright", "Copyright $1 The Oblada Authors.",
     "Copyright $1 gli autori di Oblada."},
    {"about.source",
     "Oblada's <a target=\"_blank\" href=\"$1\">source code</a> is free software, "
     "under the GNU GPL v3.",
     "Il <a target=\"_blank\" href=\"$1\">codice sorgente</a> di Oblada è software "
     "libero, con licenza GNU GPL v3."},
    {"panel.title", "Site and extensions", "Sito ed estensioni"},
    {"panel.bookmark", "Bookmark this page", "Aggiungi ai preferiti"},
    {"panel.bookmarked", "Edit bookmark", "Modifica preferito"},
    {"panel.screenshot", "Screenshot", "Screenshot"},
    {"panel.reading_mode", "Reading mode", "Modalità lettura"},
    {"panel.copy_link", "Copy link", "Copia link"},
    {"panel.install_app", "Install as app", "Installa come app"},
    {"panel.open_app", "Open in its app", "Apri nella sua app"},
    {"panel.extensions", "Extensions", "Estensioni"},
    {"panel.add_extensions", "Add extensions", "Aggiungi estensioni"},
    {"panel.manage_extensions", "Manage extensions", "Gestisci estensioni"},
    {"panel.secure", "Secure", "Sicuro"},
    {"panel.page_info", "About this page", "Informazioni sulla pagina"},
    {"firefox.install_failed", "Couldn't add the Firefox add-on",
     "Impossibile aggiungere il componente aggiuntivo di Firefox"},
});

bool IsItalian() {
  return g_browser_process &&
         g_browser_process->GetApplicationLocale().starts_with("it");
}

}  // namespace

std::string L10nUTF8(std::string_view key) {
  for (const Entry& entry : kStrings) {
    if (entry.key == key) {
      return std::string(IsItalian() ? entry.it : entry.en);
    }
  }
  return std::string(key);
}

std::u16string L10n(std::string_view key) {
  return base::UTF8ToUTF16(L10nUTF8(key));
}

}  // namespace oblada
