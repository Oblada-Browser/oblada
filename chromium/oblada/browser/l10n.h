#ifndef OBLADA_BROWSER_L10N_H_
#define OBLADA_BROWSER_L10N_H_

#include <string>
#include <string_view>

namespace oblada {

// Oblada's own UI strings, in English and Italian, picked by the browser's language.
// (Chromium's strings live in .grd files whose generated headers half the browser
// includes; adding to them would rebuild most of it for every new string.)
std::u16string L10n(std::string_view key);
std::string L10nUTF8(std::string_view key);

}  // namespace oblada

#endif  // OBLADA_BROWSER_L10N_H_
