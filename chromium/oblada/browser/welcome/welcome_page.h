#ifndef OBLADA_BROWSER_WELCOME_WELCOME_PAGE_H_
#define OBLADA_BROWSER_WELCOME_WELCOME_PAGE_H_

// oblada://welcome's files (see welcome_ui.h). The HTML's $i18n{<name>} are
// l10n.cc's "welcome.<name>" strings (listed in welcome_ui.cc).
namespace oblada::welcome {

extern const char kWelcomeHtml[];
extern const char kWelcomeCss[];
extern const char kWelcomeJs[];

}  // namespace oblada::welcome

#endif  // OBLADA_BROWSER_WELCOME_WELCOME_PAGE_H_
