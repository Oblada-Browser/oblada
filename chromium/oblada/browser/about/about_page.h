#ifndef OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_
#define OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_

namespace content {
class WebUIDataSource;
}  // namespace content

// Settings' About page (oblada://settings/help) as Oblada's: its version
// (oblada/VERSION) with the Chromium it's built on, its copyright and where its source
// code is, and help from its GitHub issues rather than Google's help center
// (chrome_pages.cc). Its update status is Oblada's updater's (oblada/browser/updater),
// which a build without one shows none of (about_handler.cc).
namespace oblada::about {

inline constexpr char16_t kSourceURL[] = u"https://github.com/Oblada-Browser/oblada";
inline constexpr char kHelpURL[] = "https://github.com/Oblada-Browser/oblada/issues";

// Replaces Chromium's About page strings in `source`, Settings' data source.
void AddAboutStrings(content::WebUIDataSource* source);

}  // namespace oblada::about

#endif  // OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_
