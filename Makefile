# Oblada's commands. They run the scripts in chromium/scripts and branding/, which hold
# the details (see chromium/README.md); `make` alone lists them.
# Written for the GNU Make 3.81 macOS ships with.

SHELL := /bin/bash
.DEFAULT_GOAL := help

SCRIPTS := chromium/scripts
VERSION_FILE := chromium/oblada/VERSION
VERSION := $(shell tr -d '[:space:]' < $(VERSION_FILE))
CHROMIUM_VERSION := $(shell tr -d '[:space:]' < chromium/CHROMIUM_VERSION)
# The build directory, wherever the scripts find it (env.sh); only read when a target
# needs it.
BUILD_DIR = $(shell source $(SCRIPTS)/env.sh && echo "$$OUT")
# Releases' build directory, an official build (build.sh --release).
RELEASE_DIR = $(shell source $(SCRIPTS)/env.sh && echo "$$RELEASE_OUT")
# Sparkle's tools, put in the checkout by sparkle.sh.
SPARKLE = $(shell source $(SCRIPTS)/env.sh && echo "$$SRC")/third_party/sparkle/bin
UPDATE_KEY := chromium/oblada/browser/updater/PUBLIC_KEY

# Options, e.g. `make test NETWORK=0`.
TARGETS ?=
RELEASE ?=
NETWORK ?=
PNG ?= /tmp/oblada.png
DO ?=
COMPACT ?=
NOTES ?=
TO ?=
FILE ?=
IPS ?=
DRY ?=

.PHONY: help sync build patches apply reset test test-updates screenshot dmg release version \
        bump update-key safe-browsing-key import-update-key free-space symbolicate icons dmg-background run run-fresh

help: ## this list
	@echo "Oblada $(VERSION), based on Chromium $(CHROMIUM_VERSION)"
	@echo
	@awk 'BEGIN { FS = ":.*## " } /^[a-z-]+:.*## / { printf "  make %-18s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

# --- The checkout and the build

sync: ## check out CHROMIUM_VERSION with its toolchains in ~/chromium, and apply the patches
	$(SCRIPTS)/sync.sh

build: ## build Oblada.app, a few minutes when incremental (TARGETS=unit_tests; RELEASE=1 the release build)
	$(SCRIPTS)/build.sh $(if $(RELEASE),--release) $(TARGETS)

patches: ## rewrite chromium/patches/ from the edits to Chromium's files in ~/chromium/src
	$(SCRIPTS)/patches.sh update

apply: ## apply the patches, overrides and renaming not applied yet (build does it too)
	$(SCRIPTS)/patches.sh apply

reset: ## revert the checkout's patched files, before moving to a new Chromium
	$(SCRIPTS)/patches.sh reset

# --- Checks: headless, they never show a window nor take the focus

test: ## the smoke test, about 40 checks (NETWORK=0 skips addons.mozilla.org; RELEASE=1)
	$(if $(NETWORK),OBLADA_SMOKE_NETWORK=$(NETWORK)) node $(SCRIPTS)/smoke.mjs $(if $(RELEASE),"$(RELEASE_DIR)/Oblada.app")

test-updates: ## the update test: packs a release with a test key, updates an older copy to it
	node $(SCRIPTS)/updates.mjs

screenshot: ## the window, browser UI included, in PNG= (DO="39001 click:..." COMPACT=1)
	node $(SCRIPTS)/screenshot.mjs $(PNG) $(if $(COMPACT),--compact) $(if $(DO),"--do=$(DO)")

# --- Releases (see CLAUDE.md, "Releases")

version: ## Oblada's version and the Chromium it's based on
	@echo "Oblada $(VERSION), based on Chromium $(CHROMIUM_VERSION)"

bump: ## set Oblada's version, e.g. make bump TO=0.1.1, before a release
	@[[ "$(TO)" =~ ^[0-9]+\.[0-9]+\.[0-9]+$$ ]] || { echo "usage: make bump TO=<x.y.z>" >&2; exit 1; }
	@echo "$(TO)" > $(VERSION_FILE)
	@echo "Oblada $(VERSION) -> $(TO) ($(VERSION_FILE)): commit it, then make release"

update-key: ## make the key releases are signed with, once: in your keychain, its public half in the app
	@[ ! -s $(UPDATE_KEY) ] || { echo "$(UPDATE_KEY) is there already" >&2; exit 1; }
	"$(SPARKLE)/generate_keys" >/dev/null
	"$(SPARKLE)/generate_keys" -p > $(UPDATE_KEY)
	@echo "the private key is in your login keychain: back it up, e.g. with"
	@echo "  \"$(SPARKLE)/generate_keys\" -x <file>   (without it, installed apps can't be updated)"
	@echo "and bring it back, or onto another Mac, with make import-update-key FILE=<file>"

safe-browsing-key: ## put releases' key for Google's Safe Browsing API in your keychain, once (asks for it)
	@source $(SCRIPTS)/env.sh && security add-generic-password -U -a oblada -s "$$SAFE_BROWSING_KEY_ITEM" \
	  -l "$$SAFE_BROWSING_KEY_ITEM" -w
	@echo "in your login keychain: release builds (build.sh --release) give it to Safe Browsing"

import-update-key: ## put the key releases are signed with in your keychain, from its backup (FILE=)
	@[ -n "$(FILE)" ] || { echo "usage: make import-update-key FILE=<private key file>" >&2; exit 1; }
	"$(SPARKLE)/generate_keys" -f "$(FILE)"
	@key="$$("$(SPARKLE)/generate_keys" -p)"; \
	if [ -s $(UPDATE_KEY) ]; then \
	  [ "$$key" = "$$(tr -d '[:space:]' < $(UPDATE_KEY))" ] || \
	    { echo "this isn't the key installed apps trust ($(UPDATE_KEY))" >&2; exit 1; }; \
	  echo "imported: it's the key installed apps trust"; \
	else \
	  echo "$$key" > $(UPDATE_KEY); \
	  echo "imported; its public half is in $(UPDATE_KEY): commit it"; \
	fi

free-space: ## free what development leaves outside the builds: tests' profiles and leftovers, unused code-sign clones, /tmp/oblada* (DRY=1 lists them)
	$(SCRIPTS)/free_space.sh $(if $(DRY),--dry-run)

dmg: ## pack the build into Oblada-silicon.dmg (and the update archive), in its directory (RELEASE=1)
	$(SCRIPTS)/dmg.sh $(if $(RELEASE),--release)

release: ## build, pack and publish the pushed commit as release <version> (NOTES=notes.md)
	@[[ -n "$(NOTES)" ]] || { echo "usage: make release NOTES=<notes.md>" >&2; exit 1; }
	$(SCRIPTS)/release.sh $(NOTES)

symbolicate: ## name Oblada's functions in a release's crash report (IPS=<report.ips>)
	@[[ -n "$(IPS)" ]] || { echo "usage: make symbolicate IPS=<report.ips>" >&2; exit 1; }
	python3 $(SCRIPTS)/symbolicate.py "$(IPS)"

# --- Branding

icons: ## redraw the logo into every icon in chromium/overrides/ (needs Inkscape, Xcode)
	branding/export.sh

dmg-background: ## redraw the DMG window's background from branding/dmg/background.html
	node branding/dmg/render.mjs

# --- Trying it out: these open a window

run: ## open the build, with your profile (RELEASE=1 the release build)
	open "$(if $(RELEASE),$(RELEASE_DIR),$(BUILD_DIR))/Oblada.app"

run-fresh: ## open the build with a new throwaway profile, as after a first install (RELEASE=1)
	open -n "$(if $(RELEASE),$(RELEASE_DIR),$(BUILD_DIR))/Oblada.app" --args --user-data-dir="$$(mktemp -d -t oblada-fresh)"
