#include "oblada/browser/barrier/farbling.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/numerics/byte_conversions.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/supports_user_data.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "crypto/hmac.h"
#include "extensions/common/url_pattern.h"
#include "net/base/schemeful_site.h"
#include "oblada/browser/barrier/barrier.h"
#include "oblada/browser/barrier/data_lists.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/gurl.h"

namespace oblada::barrier {

namespace {

// A profile's key for the session (incognito profiles have their own).
class SessionKey : public base::SupportsUserData::Data {
 public:
  static const std::array<uint8_t, 32>& Get(Profile* profile) {
    auto* data = static_cast<SessionKey*>(profile->GetUserData(&kKey));
    if (!data) {
      auto owned = std::make_unique<SessionKey>();
      base::RandBytes(owned->key_);
      data = owned.get();
      profile->SetUserData(&kKey, std::move(owned));
    }
    return data->key_;
  }

 private:
  static constexpr int kKey = 0;
  std::array<uint8_t, 32> key_;
};

// Brave's names for the APIs its exceptions leave alone, and Oblada's.
constexpr std::pair<std::string_view, FingerprintingApi> kWebcompatNames[] = {
    {"audio", kAudio},
    {"canvas", kCanvas},
    {"device-memory", kHardware},
    {"event-source-pool", kConnectionPools},
    {"hardware-concurrency", kHardware},
    {"font", kFonts},
    {"keyboard", kKeyboard},
    {"language", kLanguages},
    {"media-devices", kMediaDevices},
    {"plugins", kPlugins},
    {"screen", kScreen},
    {"speech-synthesis", kSpeechSynthesis},
    {"usb-device-serial-number", kUsb},
    {"web-sockets-pool", kConnectionPools},
    {"webgl", kWebGl},
    {"webgl2", kWebGl},
    {"webgpu", kWebGpu},
};

struct WebcompatRule {
  std::vector<URLPattern> patterns;
  uint32_t exceptions = 0;
};

std::vector<WebcompatRule> ParseWebcompatRules(std::string_view json) {
  std::vector<WebcompatRule> rules;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }
  for (const base::Value& value : parsed->GetList()) {
    const base::DictValue* dict = value.GetIfDict();
    const base::ListValue* include = dict ? dict->FindList("include") : nullptr;
    const base::ListValue* exceptions =
        dict ? dict->FindList("exceptions") : nullptr;
    if (!include || !exceptions) {
      continue;
    }
    WebcompatRule rule;
    for (const base::Value& pattern : *include) {
      URLPattern url_pattern(URLPattern::SCHEME_HTTP |
                             URLPattern::SCHEME_HTTPS);
      if (pattern.is_string() &&
          url_pattern.Parse(pattern.GetString()) ==
              URLPattern::ParseResult::kSuccess) {
        rule.patterns.push_back(std::move(url_pattern));
      }
    }
    for (const base::Value& name : *exceptions) {
      for (const auto& [brave, api] : kWebcompatNames) {
        if (name.is_string() && name.GetString() == brave) {
          rule.exceptions |= api;
        }
      }
    }
    if (!rule.patterns.empty() && rule.exceptions) {
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

std::shared_ptr<const std::vector<WebcompatRule>> WebcompatRules() {
  static base::NoDestructor<
      data_lists::Parsed<std::vector<WebcompatRule>, &ParseWebcompatRules>>
      rules(data_lists::DataList::kWebcompatExceptions);
  return rules->Get();
}

uint64_t FarblingToken(Profile* profile,
                       content::StoragePartition* partition,
                       const GURL& page_url) {
  // The container: the storage partition's name, empty for none.
  const std::string container = partition->GetConfig().partition_name();
  const std::string data = base::StrCat({container, "\n", SiteOf(page_url)});
  const auto hmac = crypto::hmac::SignSha256(SessionKey::Get(profile),
                                             base::as_byte_span(data));
  const uint64_t token =
      base::U64FromLittleEndian(base::span(hmac).first<8u>());
  return token ? token : 1;
}

uint32_t WebcompatExceptions(const GURL& page_url) {
  uint32_t exceptions = 0;
  const auto rules = WebcompatRules();
  for (const WebcompatRule& rule : *rules) {
    for (const URLPattern& pattern : rule.patterns) {
      if (pattern.MatchesURL(page_url)) {
        exceptions |= rule.exceptions;
        break;
      }
    }
  }
  return exceptions;
}

}  // namespace

FarblingSettings FarblingFor(Profile* profile,
                             content::StoragePartition* partition,
                             const GURL& page_url) {
  FarblingSettings farbling;
  const Settings settings = GetSettings(profile, page_url);
  if (settings.enabled && settings.fingerprinting) {
    farbling.token = FarblingToken(profile, partition, page_url);
    farbling.exceptions =
        settings.fingerprinting_exceptions | WebcompatExceptions(page_url);
  }
  farbling.reduce_language =
      settings.enabled && IsPrivacyOn(profile, kReduceLanguagePref);
  return farbling;
}

void GetWorkerFarbling(content::BrowserContext* browser_context,
                       content::StoragePartition* partition,
                       const blink::StorageKey& storage_key,
                       uint64_t* token,
                       uint32_t* exceptions,
                       bool* reduce_language) {
  const net::SchemefulSite& site = storage_key.top_level_site();
  if (!browser_context || !partition || site.opaque()) {
    return;
  }
  const FarblingSettings farbling =
      FarblingFor(Profile::FromBrowserContext(browser_context), partition,
                  site.GetURL());
  *token = farbling.token;
  *exceptions = farbling.exceptions;
  *reduce_language = farbling.reduce_language;
}

}  // namespace oblada::barrier
