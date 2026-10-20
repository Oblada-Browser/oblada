#include "oblada/browser/import/password_decryptor.h"

#include <windows.h>

#include <wincrypt.h>

#include <memory>

#include "base/base64.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_view_util.h"
#include "base/values.h"
#include "base/win/scoped_localalloc.h"
#include "crypto/aead.h"
#include "oblada/browser/import/import_sources.h"

namespace oblada::import {

namespace {

// "v10": AES-256-GCM, with a key kept in the browser's Local State, itself
// protected by DPAPI for the user. ("v20", Chrome's app-bound encryption, can
// only be undone by Chrome itself: those passwords are left out.)
constexpr char kPrefix[] = "v10";
constexpr char kKeyPrefix[] = "DPAPI";
constexpr size_t kNonceSize = 12;

std::optional<std::vector<uint8_t>> UnprotectData(
    base::span<const uint8_t> data) {
  DATA_BLOB input = {};
  input.pbData = const_cast<BYTE*>(data.data());
  input.cbData = static_cast<DWORD>(data.size());
  DATA_BLOB output = {};
  if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0,
                            &output)) {
    return std::nullopt;
  }
  auto memory = base::win::TakeLocalAlloc(output.pbData);
  // SAFETY: CryptUnprotectData wrote `cbData` bytes to `pbData`.
  return base::ToVector(
      UNSAFE_BUFFERS(base::span(memory.get(), output.cbData)));
}

class DpapiDecryptor : public PasswordDecryptor {
 public:
  explicit DpapiDecryptor(const Source& source) {
    // Local State is in the user data folder: the profile's parent (or the
    // folder itself, for browsers with a single profile, as Opera).
    std::string text;
    if (!base::ReadFileToString(
            source.dir.DirName().Append(FILE_PATH_LITERAL("Local State")),
            &text) &&
        !base::ReadFileToString(
            source.dir.Append(FILE_PATH_LITERAL("Local State")), &text)) {
      return;
    }
    std::optional<base::DictValue> state =
        base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
    const std::string* encoded =
        state ? state->FindStringByDottedPath("os_crypt.encrypted_key")
              : nullptr;
    std::optional<std::vector<uint8_t>> encrypted =
        encoded ? base::Base64Decode(*encoded) : std::nullopt;
    if (!encrypted || !HasPrefix(*encrypted, kKeyPrefix)) {
      return;
    }
    std::optional<std::vector<uint8_t>> key = UnprotectData(
        base::span(*encrypted).subspan(std::string_view(kKeyPrefix).size()));
    if (key) {
      key_ = std::move(*key);
    }
  }

  std::optional<std::string> Decrypt(
      base::span<const uint8_t> value) const override {
    const size_t prefix_size = std::string_view(kPrefix).size();
    if (HasPrefix(value, kPrefix) && value.size() > prefix_size + kNonceSize) {
      base::span<const uint8_t> rest = value.subspan(prefix_size);
      std::optional<std::vector<uint8_t>> plaintext = crypto::aead::Open(
          crypto::aead::AES_256_GCM, key_, rest.subspan(kNonceSize),
          rest.first(kNonceSize), {});
      if (!plaintext) {
        return std::nullopt;
      }
      return std::string(base::as_string_view(*plaintext));
    }
    // Old passwords, from before the key: protected by DPAPI directly.
    std::optional<std::vector<uint8_t>> plaintext = UnprotectData(value);
    if (!plaintext) {
      return std::nullopt;
    }
    return std::string(base::as_string_view(*plaintext));
  }

  bool HasKey() const override { return !key_.empty(); }

 private:
  std::vector<uint8_t> key_;
};

}  // namespace

// static
std::unique_ptr<PasswordDecryptor> PasswordDecryptor::Create(
    const Source& source) {
  return std::make_unique<DpapiDecryptor>(source);
}

}  // namespace oblada::import
