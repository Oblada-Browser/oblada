#include "oblada/browser/firefox_addons/firefox_addons.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/crx_file/id_util.h"
#include "components/download/public/common/download_item.h"
#include "components/services/unzip/content/unzip_service.h"
#include "components/services/unzip/public/cpp/unzip.h"
#include "components/services/unzip/public/mojom/unzipper.mojom.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "crypto/keypair.h"
#include "extensions/browser/crx_installer.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/install/crx_install_error.h"
#include "extensions/browser/install_prompt_data.h"
#include "extensions/common/extension.h"
#include "oblada/browser/extensions/api/contextual_identities/contextual_identities_api.h"
#include "oblada/browser/firefox_addons/addon_updates.h"
#include "oblada/browser/firefox_addons/xpi_conversion.h"
#include "oblada/browser/l10n.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace oblada::firefox_addons {

namespace {

// Firefox add-on id → public key of the Chromium extension made from it, so the
// add-on keeps its extension id (and its data) across versions.
constexpr char kKeysPref[] = "oblada.firefox_addons.keys";
// Firefox add-on id → where it looks for new versions, if not on addons.mozilla.org.
constexpr char kUpdateUrlsPref[] = "oblada.firefox_addons.update_urls";

#if !defined(OFFICIAL_BUILD)
// For tests, which official builds ignore (whoever could start Oblada with them
// could have it install anything): install without asking, and take add-ons
// from the tests' server as from addons.mozilla.org.
constexpr char kAutoConfirmSwitch[] = "oblada-auto-confirm-addon-install";
constexpr char kTestSourceSwitch[] = "oblada-addon-test-source";
#endif

constexpr char kAmoHost[] = "addons.mozilla.org";
// Where addons.mozilla.org's files may come from.
constexpr char kAmoCdnHost[] = "addons.cdn.mozilla.net";
constexpr char kFirefoxUserAgent[] =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:150.0) Gecko/20100101 "
    "Firefox/150.0";

// An .xpi's limits once unpacked, against archives made to fill the disk.
constexpr uint64_t kMaxUnpackedSize = 512 * 1024 * 1024;
constexpr int kMaxFiles = 20000;

std::optional<url::Origin> TestSource() {
#if !defined(OFFICIAL_BUILD)
  const GURL url(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      kTestSourceSwitch));
  if (url.is_valid()) {
    return url::Origin::Create(url);
  }
#endif
  return std::nullopt;
}

bool IsXpi(const download::DownloadItem& item) {
  return item.GetMimeType() == "application/x-xpinstall" ||
         item.GetTargetFilePath().MatchesExtension(FILE_PATH_LITERAL(".xpi"));
}

// An id for an add-on whose manifest has none, from its file name: AMO serves
// "ublock_origin-1.60.0.xpi", the same name but for the version.
std::string FallbackId(const download::DownloadItem& item) {
  std::string name =
      item.GetTargetFilePath().BaseName().RemoveFinalExtension().AsUTF8Unsafe();
  for (size_t i = 0; i + 1 < name.size(); ++i) {
    if (name[i] == '-' && base::IsAsciiDigit(name[i + 1])) {
      name.resize(i);
      break;
    }
  }
  return (name.empty() ? std::string("addon") : name) + "@oblada";
}

struct Prepared {
  base::expected<ConvertedAddon, std::string> addon;
  base::FilePath dir;
  // Base64 public key to give the add-on if it doesn't have one yet.
  std::string new_key;
};
using PreparedCallback = base::OnceCallback<void(Prepared)>;

// What an .xpi being unpacked has written so far. Its filter may run on another
// sequence than its progress.
struct UnpackState : base::RefCountedThreadSafe<UnpackState> {
  std::atomic<int> files = 0;
  std::atomic<bool> too_big = false;
  uint64_t bytes = 0;
  // Stops the unpacking.
  base::OnceClosure cancel;

 private:
  friend class base::RefCountedThreadSafe<UnpackState>;
  ~UnpackState() = default;
};

void PrepareFailed(const base::FilePath& dir,
                   std::string error,
                   PreparedCallback done) {
  if (!dir.empty()) {
    base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                               base::GetDeletePathRecursivelyCallback(dir));
  }
  std::move(done).Run(Prepared{base::unexpected(std::move(error))});
}

// Makes the unpacked add-on in `dir` an extension, with a key in case it's new.
// Blocking.
Prepared ConvertUnpacked(base::FilePath xpi,
                         base::FilePath dir,
                         std::string fallback_id) {
  Prepared prepared{Convert(dir, fallback_id), dir};
  if (!prepared.addon.has_value()) {
    base::DeletePathRecursively(dir);
    return prepared;
  }
  prepared.new_key = base::Base64Encode(
      crypto::keypair::PrivateKey::GenerateRsa2048().ToSubjectPublicKeyInfo());
  // Like an installed .crx, the downloaded .xpi isn't kept.
  base::DeleteFile(xpi);
  return prepared;
}

void OnUnpacked(base::FilePath xpi,
                base::FilePath dir,
                std::string fallback_id,
                scoped_refptr<UnpackState> state,
                PreparedCallback done,
                bool ok) {
  state->cancel.Reset();
  if (!ok || state->too_big) {
    PrepareFailed(dir,
                  state->too_big ? "The add-on is too big"
                                 : "The add-on file is damaged",
                  std::move(done));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ConvertUnpacked, std::move(xpi), std::move(dir),
                     std::move(fallback_id)),
      std::move(done));
}

void Unpack(base::FilePath xpi,
            std::string fallback_id,
            PreparedCallback done,
            base::FilePath dir,
            unzip::mojom::InfoPtr info) {
  if (!info || !info->size_is_valid || info->is_encrypted) {
    PrepareFailed(dir, "The add-on file is damaged", std::move(done));
    return;
  }
  if (info->size > kMaxUnpackedSize) {
    PrepareFailed(dir, "The add-on is too big", std::move(done));
    return;
  }
  auto state = base::MakeRefCounted<UnpackState>();
  state->cancel = unzip::Unzip(
      unzip::LaunchUnzipper(), xpi, dir, unzip::mojom::UnzipOptions::New(),
      base::BindRepeating(
          [](scoped_refptr<UnpackState> state, const base::FilePath& path) {
            if (++state->files > kMaxFiles) {
              state->too_big = true;
              return false;
            }
            return ShouldUnpack(path);
          },
          state),
      base::BindRepeating(
          [](scoped_refptr<UnpackState> state, uint64_t bytes) {
            state->bytes += bytes;
            // Its size as the archive says may be a lie.
            if (state->bytes > kMaxUnpackedSize && state->cancel) {
              state->too_big = true;
              std::move(state->cancel).Run();
            }
          },
          state),
      base::BindOnce(&OnUnpacked, xpi, dir, std::move(fallback_id), state,
                     std::move(done)));
}

// Unpacks the .xpi with Chromium's unzip service, in a sandboxed process, then
// makes it an extension.
void Prepare(base::FilePath xpi, std::string fallback_id, PreparedCallback done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([] {
        base::FilePath dir;
        return base::CreateNewTempDirectory(FILE_PATH_LITERAL("oblada-xpi"), &dir)
                   ? dir
                   : base::FilePath();
      }),
      base::BindOnce(
          [](base::FilePath xpi, std::string fallback_id, PreparedCallback done,
             base::FilePath dir) {
            if (dir.empty()) {
              PrepareFailed(dir, "Couldn't unpack the add-on", std::move(done));
              return;
            }
            unzip::GetExtractedInfo(
                unzip::LaunchUnzipper(), xpi,
                base::BindOnce(&Unpack, xpi, std::move(fallback_id),
                               std::move(done), dir));
          },
          std::move(xpi), std::move(fallback_id), std::move(done)));
}

// The active tab of `profile`'s last active window, to ask in.
content::WebContents* ActiveTab(Profile* profile) {
  BrowserWindowInterface* browser = GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  return browser && browser->GetProfile() == profile
             ? browser->GetTabStripModel()->GetActiveWebContents()
             : nullptr;
}

// Installs a converted add-on, after the usual confirmation listing what it can do, or
// silently for an update (whose new permissions, if any, wait for the user's approval
// as for any extension). An update must be the add-on it updates (`expected_id`).
void Install(base::WeakPtr<Profile> profile,
             base::WeakPtr<content::WebContents> web_contents,
             std::string expected_id,
             bool silent,
             base::OnceCallback<void(bool)> done,
             Prepared prepared) {
  content::WebContents* contents =
      web_contents ? web_contents.get()
                   : (profile ? ActiveTab(profile.get()) : nullptr);
  if (prepared.addon.has_value() && !expected_id.empty() &&
      prepared.addon->gecko_id != expected_id) {
    LOG(ERROR) << "Firefox add-on update refused: it's " << prepared.addon->gecko_id
               << ", not " << expected_id;
    prepared.addon = base::unexpected("The update is another add-on");
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock()},
        base::GetDeletePathRecursivelyCallback(prepared.dir));
  }
  if (!profile || (!contents && !silent) || !prepared.addon.has_value()) {
    if (!prepared.addon.has_value()) {
      LOG(ERROR) << "Firefox add-on not installed: " << prepared.addon.error();
      if (contents && !silent) {
        chrome::ShowWarningMessageBoxAsync(
            contents->GetTopLevelNativeWindow(), L10n("firefox.install_failed"),
            base::UTF8ToUTF16(prepared.addon.error()));
      }
    } else {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::GetDeletePathRecursivelyCallback(prepared.dir));
    }
    std::move(done).Run(false);
    return;
  }
  for (const std::string& warning : prepared.addon->warnings) {
    LOG(WARNING) << "Firefox add-on " << prepared.addon->gecko_id << ": " << warning;
  }

  const std::string* existing_key =
      profile->GetPrefs()->GetDict(kKeysPref).FindString(prepared.addon->gecko_id);
  const std::string key = existing_key ? *existing_key : prepared.new_key;
  const std::optional<std::vector<uint8_t>> public_key = base::Base64Decode(key);
  const extensions::ExtensionId id =
      crx_file::id_util::GenerateId(public_key.value_or(std::vector<uint8_t>()));

  scoped_refptr<extensions::CrxInstaller> installer =
      silent ? extensions::CrxInstaller::CreateSilent(profile.get())
             : extensions::CrxInstaller::Create(
                   profile.get(),
                   std::make_unique<ExtensionInstallPrompt>(
                       contents,
                       std::make_unique<extensions::InstallPromptData>(
                           extensions::InstallPromptData::UNSET_PROMPT_TYPE)));
  installer->set_off_store_install_allow_reason(
      extensions::CrxInstaller::OffStoreInstallAllowedBecausePref);
  installer->set_delete_source(true);
  installer->set_expected_id(id);
  installer->AddInstallerCallback(base::BindOnce(
      [](scoped_refptr<extensions::CrxInstaller> installer,
         base::WeakPtr<Profile> profile, std::string gecko_id, std::string key,
         std::string update_url, base::OnceCallback<void(bool)> done,
         const std::optional<extensions::CrxInstallError>& error) {
        if (error) {
          LOG(ERROR) << "Firefox add-on not installed: " << error->message();
        } else if (profile) {
          // Only once it's installed: an add-on the user said no to changes
          // nothing.
          ScopedDictPrefUpdate(profile->GetPrefs(), kKeysPref)->Set(gecko_id, key);
          ScopedDictPrefUpdate update_urls(profile->GetPrefs(), kUpdateUrlsPref);
          if (update_url.empty()) {
            update_urls->Remove(gecko_id);
          } else {
            update_urls->Set(gecko_id, update_url);
          }
        }
        std::move(done).Run(!error.has_value());
      },
      installer, profile, prepared.addon->gecko_id, key,
      prepared.addon->update_url, std::move(done)));
  // Installing it again (e.g. a new version from addons.mozilla.org) updates it.
  if (extensions::ExtensionRegistry::Get(profile.get())->GetInstalledExtension(id)) {
    installer->UpdateExtensionFromUnpackedCrx(id, key, prepared.dir);
  } else {
    installer->InstallUnpackedCrx(id, key, prepared.dir);
  }
}

// addons.mozilla.org only offers "Add to Firefox" to Firefox.
class AmoUserAgent : public content::WebContentsObserver,
                     public content::WebContentsUserData<AmoUserAgent> {
 public:
  ~AmoUserAgent() override = default;

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame()) {
      return;
    }
    const GURL& url = navigation->GetURL();
    const bool amo = url.SchemeIs(url::kHttpsScheme) && url.host() == kAmoHost;
    const bool overridden =
        web_contents()->GetUserAgentOverride().ua_string_override ==
        kFirefoxUserAgent;
    if (amo) {
      if (!overridden) {
        web_contents()->SetUserAgentOverride(
            blink::UserAgentOverride::UserAgentOnly(kFirefoxUserAgent),
            /*override_in_new_tabs=*/false);
      }
      navigation->SetIsOverridingUserAgent(true);
    } else if (overridden) {
      navigation->SetIsOverridingUserAgent(false);
    }
  }

 private:
  explicit AmoUserAgent(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<AmoUserAgent>(*contents) {}
  friend class content::WebContentsUserData<AmoUserAgent>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(AmoUserAgent);

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(kKeysPref);
  registry->RegisterDictionaryPref(kUpdateUrlsPref);
}

std::vector<InstalledAddon> GetInstalledAddons(Profile* profile) {
  std::vector<InstalledAddon> addons;
  const base::DictValue& update_urls = profile->GetPrefs()->GetDict(kUpdateUrlsPref);
  for (const auto [gecko_id, key] : profile->GetPrefs()->GetDict(kKeysPref)) {
    const std::optional<std::vector<uint8_t>> public_key =
        key.is_string() ? base::Base64Decode(key.GetString()) : std::nullopt;
    if (!public_key) {
      continue;
    }
    const extensions::Extension* extension =
        extensions::ExtensionRegistry::Get(profile)->GetInstalledExtension(
            crx_file::id_util::GenerateId(*public_key));
    if (!extension) {
      continue;
    }
    // A newer version may be downloaded already, waiting for the add-on to be idle
    // (or for the next start) to be installed, as Chromium does with extensions in use.
    base::Version version = extension->version();
    if (std::optional<extensions::ExtensionPrefs::InstallRecord> pending =
            extensions::ExtensionPrefs::Get(profile)->GetDelayedInstallExtensionInfo(
                extension->id());
        pending && pending->extension_manifest) {
      if (const std::string* pending_version =
              pending->extension_manifest->FindString("version")) {
        if (base::Version newer(*pending_version);
            newer.IsValid() && newer > version) {
          version = std::move(newer);
        }
      }
    }
    const std::string* update_url = update_urls.FindString(gecko_id);
    addons.push_back({gecko_id, std::move(version),
                      update_url ? GURL(*update_url) : GURL()});
  }
  return addons;
}

bool IsAddonSource(const GURL& url) {
  if (url.SchemeIs(url::kHttpsScheme) &&
      (url.host() == kAmoHost || url.host() == kAmoCdnHost)) {
    return true;
  }
  const std::optional<url::Origin> test = TestSource();
  return test && test->IsSameOriginWith(url);
}

void InstallUpdate(Profile* profile,
                   const base::FilePath& xpi,
                   const std::string& gecko_id) {
  Prepare(xpi, gecko_id,
          base::BindOnce(&Install, profile->GetWeakPtr(),
                         base::WeakPtr<content::WebContents>(), gecko_id,
                         /*silent=*/true, base::DoNothingAs<void(bool)>()));
}

bool IsAddonDownload(Profile* profile, const download::DownloadItem& item) {
  if (!IsXpi(item) || profile->IsOffTheRecord()) {
    return false;
  }
  // Only from addons.mozilla.org, which reviews and signs add-ons, as Chrome
  // takes extensions from its Web Store only, and only started there or by the
  // user: an .xpi from anywhere else is just a file.
  const std::optional<url::Origin>& initiator = item.GetRequestInitiator();
  if (initiator && !IsAddonSource(initiator->GetURL())) {
    return false;
  }
  const std::vector<GURL>& chain = item.GetUrlChain();
  return !chain.empty() && std::ranges::all_of(chain, [](const GURL& url) {
    return IsAddonSource(url);
  });
}

void InstallDownload(Profile* profile,
                     download::DownloadItem* item,
                     base::OnceCallback<void(bool)> done) {
  content::WebContents* contents = content::DownloadItemUtils::GetWebContents(item);
  Prepare(item->GetFullPath(), FallbackId(*item),
          base::BindOnce(&Install, profile->GetWeakPtr(),
                         contents ? contents->GetWeakPtr()
                                  : base::WeakPtr<content::WebContents>(),
                         std::string(), /*silent=*/false, std::move(done)));
}

void AttachTabHelpers(content::WebContents* contents) {
  AmoUserAgent::CreateForWebContents(contents);
  // Once a profile is in use, its add-ons are kept up to date.
  StartAddonUpdates(Profile::FromBrowserContext(contents->GetBrowserContext()));
}

void Init() {
  StartContextualIdentitiesEvents();
#if !defined(OFFICIAL_BUILD)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kAutoConfirmSwitch)) {
    static base::NoDestructor<extensions::ScopedTestDialogAutoConfirm> confirm(
        extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  }
#endif
}

}  // namespace oblada::firefox_addons
