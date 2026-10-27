// Brave's ad-blocking engine, adblock-rust (the `adblock` crate, MPL-2.0), for
// Oblada's C++ through cxx: see adblock_engine.h, which wraps it.

use std::collections::HashSet;

use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::resources::{PermissionMask, Resource};
use adblock::url_parser::ResolvesDomain;
use cxx::{CxxString, CxxVector};

#[cxx::bridge(namespace = "oblada::barrier::adblock::ffi")]
mod ffi {
    // What a request should do, by Engine::Check()'s rules.
    struct MatchResult {
        // A blocking rule matched, and no exception unless it's important.
        should_block: bool,
        // A blocking rule matched (exceptions or not).
        matched: bool,
        // An exception rule matched.
        has_exception: bool,
        important: bool,
        // A data: URL to answer with instead, if any.
        redirect: String,
        // The URL without the parameters a $removeparam rule removes, if any.
        rewritten_url: String,
    }

    // A list's header (`! Title:`, `! Expires:`).
    struct ListMetadata {
        title: String,
        homepage: String,
        // 0 when the list doesn't say.
        expires_hours: u32,
    }

    unsafe extern "C++" {
        include!("oblada/components/barrier/adblock/domain_resolver.h");

        // Where `host`'s registrable domain (eTLD+1) is in it, as byte
        // offsets, from Chromium's public suffix list, for the engine's URL
        // parsing.
        fn ResolveDomainPosition(host: &str, start: &mut usize, end: &mut usize);
    }

    extern "Rust" {
        type FilterSetBuilder;
        type EngineHandle;

        fn new_filter_set() -> Box<FilterSetBuilder>;
        // `permissions` lets the list's rules inject the scriptlets that need
        // them (bit 0: uBlock Origin's trusted ones).
        fn add_list(self: &mut FilterSetBuilder, text: &[u8], permissions: u8);

        fn new_engine(set: Box<FilterSetBuilder>) -> Box<EngineHandle>;
        fn new_empty_engine() -> Box<EngineHandle>;
        fn serialize(self: &EngineHandle) -> Vec<u8>;
        fn deserialize(self: &mut EngineHandle, data: &[u8]) -> bool;
        // `json`, a list of resources in adblock-rust's format, for $redirect
        // rules and scriptlets. False if it isn't one.
        fn use_resources(self: &mut EngineHandle, json: &[u8]) -> bool;
        fn enable_tag(self: &mut EngineHandle, tag: &str, enabled: bool);

        fn check(
            self: &EngineHandle,
            url: &str,
            hostname: &str,
            source_hostname: &str,
            request_type: &str,
            third_party: bool,
            method: &str,
            previously_matched_rule: bool,
            force_check_exceptions: bool,
        ) -> MatchResult;
        fn csp_directives(
            self: &EngineHandle,
            url: &str,
            hostname: &str,
            source_hostname: &str,
            request_type: &str,
            third_party: bool,
        ) -> String;
        // The page's cosmetic resources (selectors to hide, procedural filters,
        // scriptlets, exceptions, generichide), as JSON.
        fn url_cosmetic_resources(self: &EngineHandle, url: &str) -> String;
        fn hidden_class_id_selectors(
            self: &EngineHandle,
            classes: &CxxVector<CxxString>,
            ids: &CxxVector<CxxString>,
            exceptions: &CxxVector<CxxString>,
        ) -> Vec<String>;

        fn read_list_metadata(text: &[u8]) -> ListMetadata;
        // Sets the engine's domain resolver to ResolveDomainPosition(), once.
        fn init_domain_resolver();
    }
}

use ffi::{ListMetadata, MatchResult};

struct ChromiumDomainResolver;

impl ResolvesDomain for ChromiumDomainResolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let (mut start, mut end) = (0, 0);
        ffi::ResolveDomainPosition(host, &mut start, &mut end);
        (start, end)
    }
}

fn init_domain_resolver() {
    let _ = adblock::url_parser::set_domain_resolver(Box::new(ChromiumDomainResolver));
}

pub struct FilterSetBuilder(FilterSet);

fn new_filter_set() -> Box<FilterSetBuilder> {
    Box::new(FilterSetBuilder(FilterSet::new(false)))
}

impl FilterSetBuilder {
    fn add_list(&mut self, text: &[u8], permissions: u8) {
        self.0.add_filter_list(
            String::from_utf8_lossy(text).into_owned(),
            ParseOptions {
                permissions: PermissionMask::from_bits(permissions),
                ..ParseOptions::default()
            },
        );
    }
}

pub struct EngineHandle(adblock::Engine);

fn new_engine(set: Box<FilterSetBuilder>) -> Box<EngineHandle> {
    Box::new(EngineHandle(adblock::Engine::new_with_filter_set(set.0)))
}

fn new_empty_engine() -> Box<EngineHandle> {
    Box::new(EngineHandle(adblock::Engine::default()))
}

fn to_set(strings: &CxxVector<CxxString>) -> HashSet<String> {
    strings.iter().map(|s| s.to_string_lossy().into_owned()).collect()
}

impl EngineHandle {
    fn serialize(&self) -> Vec<u8> {
        self.0.serialize()
    }

    fn deserialize(&mut self, data: &[u8]) -> bool {
        self.0.deserialize(data).is_ok()
    }

    fn use_resources(&mut self, json: &[u8]) -> bool {
        match serde_json::from_slice::<Vec<Resource>>(json) {
            Ok(resources) => {
                self.0.use_resources(resources);
                true
            }
            Err(_) => false,
        }
    }

    fn enable_tag(&mut self, tag: &str, enabled: bool) {
        if enabled {
            self.0.enable_tags(&[tag]);
        } else {
            self.0.disable_tags(&[tag]);
        }
    }

    fn check(
        &self,
        url: &str,
        hostname: &str,
        source_hostname: &str,
        request_type: &str,
        third_party: bool,
        method: &str,
        previously_matched_rule: bool,
        force_check_exceptions: bool,
    ) -> MatchResult {
        let request =
            Request::preparsed(url, hostname, source_hostname, request_type, third_party, method);
        let result = self.0.check_network_request_subset(
            &request,
            previously_matched_rule,
            force_check_exceptions,
        );
        MatchResult {
            should_block: result.should_block(),
            matched: result.filter.is_some(),
            has_exception: result.exception.is_some(),
            important: result.important,
            redirect: result.redirect.unwrap_or_default(),
            rewritten_url: result.rewritten_url.unwrap_or_default(),
        }
    }

    fn csp_directives(
        &self,
        url: &str,
        hostname: &str,
        source_hostname: &str,
        request_type: &str,
        third_party: bool,
    ) -> String {
        let request =
            Request::preparsed(url, hostname, source_hostname, request_type, third_party, "get");
        self.0.get_csp_directives(&request).unwrap_or_default()
    }

    fn url_cosmetic_resources(&self, url: &str) -> String {
        serde_json::to_string(&self.0.url_cosmetic_resources(url)).unwrap_or_default()
    }

    fn hidden_class_id_selectors(
        &self,
        classes: &CxxVector<CxxString>,
        ids: &CxxVector<CxxString>,
        exceptions: &CxxVector<CxxString>,
    ) -> Vec<String> {
        let classes: Vec<String> = to_set(classes).into_iter().collect();
        let ids: Vec<String> = to_set(ids).into_iter().collect();
        self.0.hidden_class_id_selectors(&classes, &ids, &to_set(exceptions))
    }
}

fn read_list_metadata(text: &[u8]) -> ListMetadata {
    let metadata = adblock::lists::read_list_metadata(&String::from_utf8_lossy(text));
    let expires_hours = match metadata.expires {
        Some(adblock::lists::ExpiresInterval::Hours(hours)) => u32::from(hours),
        Some(adblock::lists::ExpiresInterval::Days(days)) => u32::from(days) * 24,
        None => 0,
    };
    ListMetadata {
        title: metadata.title.unwrap_or_default(),
        homepage: metadata.homepage.unwrap_or_default(),
        expires_hours,
    }
}
