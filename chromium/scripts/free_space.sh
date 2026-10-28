#!/usr/bin/env bash
# Frees the space development leaves outside the builds; all of it is made again when needed:
# - test runs' Oblada left running when their test was stopped (the test copy's, whose
#   parent is then launchd: never the user's Oblada), and their profiles and folders in
#   the temporary folder (smoke.mjs, updates.mjs, screenshot.mjs, others' oblada-*), and
#   add-ons Oblada unpacked there over an hour ago;
# - the test copy's and the update test's settings and caches in ~/Library;
# - Oblada's code-sign clones (/var/folders/…/X) that no running Oblada uses;
# - /tmp/oblada*: logs, notes and screenshots of past runs.
# Never anything of the builds (the checkout's out/ directories: objects, test copies,
# packed releases, caches) or the website's, the user's profile
# (~/Library/Application Support/Oblada) or its caches, nor releases' symbols. Refuses
# while a test, a build or a release runs.
#   free_space.sh [--dry-run]    --dry-run: only lists what it would remove
set -euo pipefail

DRY_RUN=false
[ "${1:-}" = --dry-run ] && DRY_RUN=true

# The test copy's main processes, with their parent: a test's own, or left behind
# (launchd's) by a test that was stopped.
# (By their executable, not their arguments, which any command may mention.)
TEST_PROCESSES="$(ps -axo pid=,ppid=,comm= | awk '/Oblada Test\.app\/Contents\/MacOS\/Oblada$/')"
if pgrep -f "scripts/(smoke|updates|screenshot)\.mjs|scripts/(release|dmg|build)\.sh" >/dev/null ||
   awk 'NF && $2 != 1 { found = 1 } END { exit !found }' <<< "$TEST_PROCESSES"; then
  echo "a test, a build or a release is running: try again once it's done" >&2
  exit 1
fi

TMP="$(getconf DARWIN_USER_TEMP_DIR)"
TMP="${TMP%/}"
FREED=0

# Removes `path` (a file or a folder), saying what it was and how big.
remove() {
  local path="$1" what="$2" kb
  [ -e "$path" ] || return 0
  kb="$(du -sk "$path" 2>/dev/null | cut -f1)"
  FREED=$((FREED + ${kb:-0}))
  printf '%8s  %s (%s)\n' "$(numfmt "${kb:-0}")" "$path" "$what"
  $DRY_RUN || rm -rf "$path"
}

numfmt() {
  local kb="$1"
  if [ "$kb" -ge 1048576 ]; then
    awk -v k="$kb" 'BEGIN { printf "%.1f GB", k / 1048576 }'
  elif [ "$kb" -ge 1024 ]; then
    awk -v k="$kb" 'BEGIN { printf "%.0f MB", k / 1024 }'
  else
    echo "$kb KB"
  fi
}

# Test runs' Oblada left running, which keep their profiles open.
LEFT="$(awk '$2 == 1 { print $1 }' <<< "$TEST_PROCESSES")"
for pid in $LEFT; do
  if $DRY_RUN; then
    echo "would stop a test's Oblada left running (pid $pid)"
  else
    echo "stopping a test's Oblada left running (pid $pid)"
    kill "$pid" 2>/dev/null || true
  fi
done
if [ -n "$LEFT" ] && ! $DRY_RUN; then
  sleep 3
fi

# Test runs' folders; add-ons being unpacked by a running Oblada are recent.
while IFS= read -r path; do
  remove "$path" "a test's temporary folder"
done < <(find "$TMP" -maxdepth 1 -name 'oblada-*' ! -name 'oblada-xpi*' 2>/dev/null)
while IFS= read -r path; do
  remove "$path" "an add-on unpacked for installing"
done < <(find "$TMP" -maxdepth 1 -name 'oblada-xpi*' -mmin +60 2>/dev/null)

# The test copy's and the update test's settings and caches (updates.mjs removes its
# own, unless it was stopped).
for path in "$HOME"/Library/Preferences/dev.oblada.Oblada.test.plist \
            "$HOME"/Library/Preferences/dev.oblada.Oblada.update-test.plist \
            "$HOME"/Library/Caches/dev.oblada.Oblada.update-test \
            "$HOME"/Library/HTTPStorages/dev.oblada.Oblada.update-test; do
  remove "$path" "a test app's settings"
done

# Oblada's code-sign clones: Chromium clones the app at each launch and removes the
# clone when it quits normally; those of instances that were killed stay.
RUNNING="$(ps -axo command)"
for clone in "$(dirname "$TMP")"/X/dev.oblada.Oblada*.code_sign_clone/*; do
  [ -d "$clone" ] || continue
  if grep -qF "$clone/" <<< "$RUNNING"; then
    continue
  fi
  remove "$clone" "a code-sign clone no Oblada uses"
done

# Past runs' logs, notes and screenshots.
for path in /tmp/oblada*; do
  remove "$path" "a past run's file"
done

if $DRY_RUN; then
  echo "would free $(numfmt "$FREED")"
else
  echo "freed $(numfmt "$FREED")"
fi
