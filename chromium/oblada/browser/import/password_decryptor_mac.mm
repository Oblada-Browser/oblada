#include "oblada/browser/import/password_decryptor.h"

#include <memory>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/strings/string_view_util.h"
#include "base/types/expected.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "crypto/apple/fake_keychain_v2.h"
#include "crypto/apple/keychain_v2.h"
#include "oblada/browser/import/import_sources.h"

namespace oblada::import {

namespace {

// "v10": AES-128-CBC, with a key derived from the password the browser keeps
// in the Keychain ("Chrome Safe Storage").
constexpr char kPrefix[] = "v10";
constexpr int kIterations = 1003;

class KeychainDecryptor : public PasswordDecryptor {
 public:
  explicit KeychainDecryptor(const Source& source) {
    // Tests never touch the user's Keychain (see --use-mock-keychain).
    std::unique_ptr<crypto::apple::FakeKeychainV2> fake;
    crypto::apple::KeychainV2* keychain =
        &crypto::apple::KeychainV2::GetInstance();
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            os_crypt::switches::kUseMockKeychain)) {
      fake = std::make_unique<crypto::apple::FakeKeychainV2>("oblada-import");
      keychain = fake.get();
    }
    base::expected<std::vector<uint8_t>, OSStatus> password =
        keychain->FindGenericPassword(source.safe_storage,
                                      source.safe_storage_account);
    if (password.has_value() && !password->empty()) {
      key_ = DeriveCbcKey(std::string(base::as_string_view(*password)),
                          kIterations);
    }
  }

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
  return std::make_unique<KeychainDecryptor>(source);
}

}  // namespace oblada::import
