#ifndef OBLADA_BROWSER_IMPORT_PASSWORD_DECRYPTOR_H_
#define OBLADA_BROWSER_IMPORT_PASSWORD_DECRYPTOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace oblada::import {

struct Source;

// Browsers of Chromium's family keep their passwords encrypted with a key of
// their own, kept by the system: in macOS's Keychain, protected by Windows'
// DPAPI, or on Linux a fixed one ("v10") or one in the desktop's secret
// service ("v11"). Reads the key once, then decrypts the passwords of
// `source`'s Login Data with it.
class PasswordDecryptor {
 public:
  // Blocks. On macOS, the system asks the user to let Oblada read the key (it
  // is another app's Keychain item).
  static std::unique_ptr<PasswordDecryptor> Create(const Source& source);

  virtual ~PasswordDecryptor() = default;

  // The password, or nothing if it can't be decrypted.
  virtual std::optional<std::string> Decrypt(
      base::span<const uint8_t> value) const = 0;

  // Whether it has the key: not when the user didn't let Oblada read it, say.
  virtual bool HasKey() const = 0;

 protected:
  // Whether `value` starts with `prefix` (the format's version, "v10").
  static bool HasPrefix(base::span<const uint8_t> value,
                        std::string_view prefix);

  // AES-128-CBC, as macOS and Linux encrypt passwords: the key is derived from
  // `password` as Chromium's os_crypt does.
  static std::vector<uint8_t> DeriveCbcKey(const std::string& password,
                                           int iterations);
  static std::optional<std::string> DecryptCbc(
      base::span<const uint8_t> key,
      base::span<const uint8_t> ciphertext);
};

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_PASSWORD_DECRYPTOR_H_
