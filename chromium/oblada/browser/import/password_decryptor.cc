#include "oblada/browser/import/password_decryptor.h"

#include <array>

#include "base/containers/span.h"
#include "base/strings/string_view_util.h"
#include "crypto/aes_cbc.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace oblada::import {

namespace {

// As in Chromium's os_crypt (components/os_crypt), on macOS and Linux.
constexpr std::array<uint8_t, 9> kSalt = {'s', 'a', 'l', 't', 'y',
                                          's', 'a', 'l', 't'};
constexpr size_t kKeySize = 16;

}  // namespace

// static
bool PasswordDecryptor::HasPrefix(base::span<const uint8_t> value,
                                  std::string_view prefix) {
  return value.size() >= prefix.size() &&
         base::as_string_view(value.first(prefix.size())) == prefix;
}

// static
std::vector<uint8_t> PasswordDecryptor::DeriveCbcKey(
    const std::string& password,
    int iterations) {
  std::vector<uint8_t> key(kKeySize);
  if (!PKCS5_PBKDF2_HMAC_SHA1(password.data(), password.size(), kSalt.data(),
                              kSalt.size(), iterations, key.size(),
                              key.data())) {
    key.clear();
  }
  return key;
}

// static
std::optional<std::string> PasswordDecryptor::DecryptCbc(
    base::span<const uint8_t> key,
    base::span<const uint8_t> ciphertext) {
  if (key.empty()) {
    return std::nullopt;
  }
  // os_crypt's initialization vector: 16 spaces.
  std::array<uint8_t, crypto::aes_cbc::kBlockSize> iv;
  iv.fill(' ');
  std::optional<std::vector<uint8_t>> plaintext =
      crypto::aes_cbc::Decrypt(key, iv, ciphertext);
  if (!plaintext) {
    return std::nullopt;
  }
  return std::string(base::as_string_view(*plaintext));
}

}  // namespace oblada::import
