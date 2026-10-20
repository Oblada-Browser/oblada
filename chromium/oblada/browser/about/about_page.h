#ifndef OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_
#define OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_

namespace content {
class WebUIDataSource;
}  // namespace content

// Settings' About page (oblada://settings/help) as Oblada's: its version
// (oblada/VERSION) with the Chromium it's built on, its copyright and where its source
// code is, and help from its GitHub issues rather than Google's help center
// (chrome_pages.cc). There's no updater: new versions are GitHub releases, and the page
// shows no update status (about_handler.cc) rather than a failed check.
namespace oblada::about {

inline constexpr char16_t kSourceURL[] = u"https://github.com/Oblada-Browser/oblada";
inline constexpr char kHelpURL[] = "https://github.com/Oblada-Browser/oblada/issues";

// Replaces Chromium's About page strings in `source`, Settings' data source.
void AddAboutStrings(content::WebUIDataSource* source);

}  // namespace oblada::about

#endif  // OBLADA_BROWSER_ABOUT_ABOUT_PAGE_H_
