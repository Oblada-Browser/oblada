#include "oblada/components/barrier/adblock/adblock_engine.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "oblada/components/barrier/adblock/lib.rs.h"

namespace oblada::barrier::adblock {

namespace {

rust::Slice<const uint8_t> ToSlice(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

rust::Str ToStr(std::string_view text) {
  return {text.data(), text.size()};
}

void InitDomainResolverOnce() {
  [[maybe_unused]] static const bool initialized = [] {
    ffi::init_domain_resolver();
    return true;
  }();
}

std::vector<std::string> ToStrings(const rust::Vec<rust::String>& strings) {
  std::vector<std::string> result;
  result.reserve(strings.size());
  for (const rust::String& string : strings) {
    result.emplace_back(string.data(), string.size());
  }
  return result;
}

}  // namespace

// static
std::unique_ptr<Engine> Engine::FromLists(const std::vector<List>& lists) {
  InitDomainResolverOnce();
  rust::Box<ffi::FilterSetBuilder> set = ffi::new_filter_set();
  for (const List& list : lists) {
    set->add_list(ToSlice(list.text), list.permissions);
  }
  return base::WrapUnique(new Engine(ffi::new_engine(std::move(set))));
}

// static
std::unique_ptr<Engine> Engine::Deserialize(base::span<const uint8_t> data) {
  InitDomainResolverOnce();
  rust::Box<ffi::EngineHandle> handle = ffi::new_empty_engine();
  if (!handle->deserialize({data.data(), data.size()})) {
    return nullptr;
  }
  return base::WrapUnique(new Engine(std::move(handle)));
}

// static
std::unique_ptr<Engine> Engine::Empty() {
  InitDomainResolverOnce();
  return base::WrapUnique(new Engine(ffi::new_empty_engine()));
}

Engine::Engine(rust::Box<ffi::EngineHandle> handle) : handle_(std::move(handle)) {}

Engine::~Engine() = default;

std::vector<uint8_t> Engine::Serialize() const {
  rust::Vec<uint8_t> data = handle_->serialize();
  return std::vector<uint8_t>(data.begin(), data.end());
}

bool Engine::UseResources(std::string_view json) {
  return handle_->use_resources(ToSlice(json));
}

void Engine::EnableTag(std::string_view tag, bool enabled) {
  handle_->enable_tag(ToStr(tag), enabled);
}

Engine::Match Engine::Check(const Request& request) const {
  ffi::MatchResult result = handle_->check(
      ToStr(request.url), ToStr(request.hostname),
      ToStr(request.source_hostname), ToStr(request.type),
      request.third_party, ToStr(request.method),
      request.previously_matched_rule, request.force_check_exceptions);
  return {.should_block = result.should_block,
          .matched = result.matched,
          .has_exception = result.has_exception,
          .important = result.important,
          .redirect = std::string(result.redirect),
          .rewritten_url = std::string(result.rewritten_url)};
}

std::string Engine::CspDirectives(const Request& request) const {
  return std::string(handle_->csp_directives(
      ToStr(request.url), ToStr(request.hostname),
      ToStr(request.source_hostname), ToStr(request.type),
      request.third_party));
}

std::string Engine::CosmeticResources(std::string_view url) const {
  return std::string(handle_->url_cosmetic_resources(ToStr(url)));
}

std::vector<std::string> Engine::HiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) const {
  return ToStrings(
      handle_->hidden_class_id_selectors(classes, ids, exceptions));
}

ListMetadata ReadListMetadata(std::string_view text) {
  ffi::ListMetadata metadata = ffi::read_list_metadata(ToSlice(text));
  return {.title = std::string(metadata.title),
          .homepage = std::string(metadata.homepage),
          .expires_hours = metadata.expires_hours};
}

}  // namespace oblada::barrier::adblock
