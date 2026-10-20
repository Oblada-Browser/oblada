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

# Options, e.g. `make test NETWORK=0`.
TARGETS ?=
NETWORK ?=
PNG ?= /tmp/oblada.png
DO ?=
COMPACT ?=
NOTES ?=
TO ?=

.PHONY: help sync build patches apply reset test screenshot dmg release version bump \
        icons dmg-background run run-fresh

help: ## this list
	@echo "Oblada $(VERSION), based on Chromium $(CHROMIUM_VERSION)"
	@echo
	@awk 'BEGIN { FS = ":.*## " } /^[a-z-]+:.*## / { printf "  make %-16s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

# --- The checkout and the build

sync: ## check out CHROMIUM_VERSION with its toolchains in ~/chromium, and apply the patches
	$(SCRIPTS)/sync.sh

build: ## build Oblada.app, a few minutes when incremental (TARGETS=unit_tests for others)
	$(SCRIPTS)/build.sh $(TARGETS)

patches: ## rewrite chromium/patches/ from the edits to Chromium's files in ~/chromium/src
	$(SCRIPTS)/patches.sh update

apply: ## apply the patches, overrides and renaming not applied yet (build does it too)
	$(SCRIPTS)/patches.sh apply

reset: ## revert the checkout's patched files, before moving to a new Chromium
	$(SCRIPTS)/patches.sh reset

# --- Checks: headless, they never show a window nor take the focus

test: ## the smoke test, about 40 checks (NETWORK=0 skips addons.mozilla.org)
	$(if $(NETWORK),OBLADA_SMOKE_NETWORK=$(NETWORK)) node $(SCRIPTS)/smoke.mjs

screenshot: ## the window, browser UI included, in PNG= (DO="39001 click:..." COMPACT=1)
	node $(SCRIPTS)/screenshot.mjs $(PNG) $(if $(COMPACT),--compact) $(if $(DO),"--do=$(DO)")

# --- Releases (see CLAUDE.md, "Releases")

version: ## Oblada's version and the Chromium it's based on
	@echo "Oblada $(VERSION), based on Chromium $(CHROMIUM_VERSION)"

bump: ## set Oblada's version, e.g. make bump TO=0.1.1, before a release
	@[[ "$(TO)" =~ ^[0-9]+\.[0-9]+\.[0-9]+$$ ]] || { echo "usage: make bump TO=<x.y.z>" >&2; exit 1; }
	@echo "$(TO)" > $(VERSION_FILE)
	@echo "Oblada $(VERSION) -> $(TO) ($(VERSION_FILE)): commit it, then make release"

dmg: ## pack the build into Oblada-<version>-arm64.dmg, in the build directory
	$(SCRIPTS)/dmg.sh

release: ## build, pack and publish the pushed commit as release <version> (NOTES=notes.md)
	@[[ -n "$(NOTES)" ]] || { echo "usage: make release NOTES=<notes.md>" >&2; exit 1; }
	$(SCRIPTS)/release.sh $(NOTES)

# --- Branding

icons: ## redraw the logo into every icon in chromium/overrides/ (needs Inkscape, Xcode)
	branding/export.sh

dmg-background: ## redraw the DMG window's background from branding/dmg/background.html
	node branding/dmg/render.mjs

# --- Trying it out: these open a window

run: ## open the build, with your profile
	open "$(BUILD_DIR)/Oblada.app"

run-fresh: ## open the build with a new throwaway profile, as after a first install
	open -n "$(BUILD_DIR)/Oblada.app" --args --user-data-dir="$$(mktemp -d -t oblada-fresh)"
