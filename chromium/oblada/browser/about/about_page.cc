#include "oblada/browser/about/about_page.h"

#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_branded_strings.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_ui_data_source.h"
#include "oblada/browser/l10n.h"
#include "oblada/version.h"
#include "ui/base/l10n/l10n_util.h"

namespace oblada::about {

namespace {

// The processor the build is for, as Chromium's About page names it.
constexpr char16_t kArchitecture[] =
#if defined(ARCH_CPU_ARM64)
    u"arm64";
#elif defined(ARCH_CPU_X86_64)
    u"x86_64";
#else
    u"";
#endif

}  // namespace

void AddAboutStrings(content::WebUIDataSource* source) {
  // Oblada's version, and the Chromium it is built on.
  source->AddString("aboutBrowserVersion",
                    base::ReplaceStringPlaceholders(
                        L10n("about.version"),
                        std::vector<std::u16string>{
                            u"" OBLADA_VERSION,
                            base::UTF8ToUTF16(version_info::GetVersionNumber()),
                            kArchitecture},
                        nullptr));

  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  source->AddString(
      "aboutProductCopyright",
      base::ReplaceStringPlaceholders(
          L10n("about.copyright"),
          std::vector<std::u16string>{base::NumberToString16(now.year)}, nullptr));

  // Chromium's credits ("made possible by the Chromium open source project and
  // other open source software"), then Oblada's own source code.
  const std::u16string chromium = l10n_util::GetStringFUTF16(
      IDS_VERSION_UI_LICENSE, chrome::kChromiumProjectURL,
      chrome::kChromeUICreditsURL16,
      l10n_util::GetStringUTF16(IDS_SETTINGS_OPENS_IN_NEW_TAB));
  source->AddString(
      "aboutProductLicense",
      base::StrCat({chromium, u" ",
                    base::ReplaceStringPlaceholders(
                        L10n("about.source"),
                        std::vector<std::u16string>{kSourceURL}, nullptr)}));
}

}  // namespace oblada::about
