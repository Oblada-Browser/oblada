#include "oblada/browser/import/password_decryptor.h"

#include <memory>

#include "base/containers/span.h"
#include "oblada/browser/import/import_sources.h"

namespace oblada::import {

namespace {

// "v10": AES-128-CBC, with a key derived from a fixed password, used when the
// browser found no secret service. "v11" uses one kept in the desktop's secret
// service (GNOME Keyring, KWallet), which isn't read yet: those passwords are
// left out.
constexpr char kPrefix[] = "v10";
constexpr char kFixedPassword[] = "peanuts";
constexpr int kIterations = 1;

class FixedKeyDecryptor : public PasswordDecryptor {
 public:
  FixedKeyDecryptor() : key_(DeriveCbcKey(kFixedPassword, kIterations)) {}

  std::optional<std::string> Decrypt(
      base::span<const uint8_t> value) const override {
    if (!HasPrefix(value, kPrefix)) {
      return std::nullopt;
    }
    return DecryptCbc(key_, value.subspan(std::string_view(kPrefix).size()));
  }

  bool HasKey() const override { return !key_.empty(); }

 private:
  std::vector<uint8_t> key_;
};

}  // namespace

// static
std::unique_ptr<PasswordDecryptor> PasswordDecryptor::Create(
    const Source& source) {
  return std::make_unique<FixedKeyDecryptor>();
}

}  // namespace oblada::import
