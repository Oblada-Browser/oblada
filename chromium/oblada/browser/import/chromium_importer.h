#ifndef OBLADA_BROWSER_IMPORT_CHROMIUM_IMPORTER_H_
#define OBLADA_BROWSER_IMPORT_CHROMIUM_IMPORTER_H_

#include <cstdint>

#include "base/functional/callback_forward.h"

class Profile;

namespace oblada::import {

struct Source;

// What an import brought in: -1 for what wasn't asked for.
struct Imported {
  int bookmarks = -1;
  int history = -1;
  int passwords = -1;
  // Passwords there were, but which couldn't be read: their key wasn't given,
  // or they are in a format Oblada can't read (see password_decryptor.h).
  int passwords_unreadable = 0;
};

// Imports `items` (user_data_importer::ImportItem bits) from `source`, a
// browser of Chromium's family, into `profile`: reads copies of its files in
// the background (the browser may be running, and holding them), then adds
// what it found as Chromium's own importers do. Bookmarks go to the bookmarks
// bar if it is empty, or else to a folder of their own.
void ImportFromChromium(Profile* profile,
                        const Source& source,
                        uint16_t items,
                        base::OnceCallback<void(const Imported&)> done);

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_CHROMIUM_IMPORTER_H_
