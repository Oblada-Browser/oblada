#!/usr/bin/env bash
# Renders the logo into every file Chromium's Mac build takes Oblada's branding from. They land
# in chromium/overrides/, at the same paths as in the checkout: they are images, which patches
# can't carry, so patches.sh apply copies them over Chromium's own.
# Needs Inkscape (brew install --cask inkscape) and Xcode (actool).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
THEME="$HERE/../chromium/overrides/chrome/app/theme"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 "$HERE/logo.py"

png() { # <svg> <size> <out>
  mkdir -p "$(dirname "$3")"
  inkscape "$1" --export-type=png --export-filename="$3" --export-width="$2" 2>/dev/null
}

# The app icon. Assets.car holds the Liquid Glass layers macOS 26 and later draw, plus the flat
# renderings actool makes from them for older systems; app.icns is the oldest fallback.
xcrun actool "$HERE/AppIcon.icon" --compile "$WORK" --output-partial-info-plist "$WORK/info.plist" \
  --app-icon AppIcon --platform macosx --target-device mac --minimum-deployment-target 13.0 >/dev/null
mkdir -p "$THEME/chromium/mac"
cp "$WORK/Assets.car" "$THEME/chromium/mac/Assets.car"
cp "$WORK/AppIcon.icns" "$THEME/chromium/mac/app.icns"

# The logo inside the browser: About, the profile picker, shortcuts, notifications.
for size in 16 24 48 64 128 256; do
  png "$HERE/oblada-tile.svg" "$size" "$THEME/chromium/product_logo_$size.png"
done
for size in 16 32; do
  png "$HERE/oblada-tile.svg" "$size" "$THEME/default_100_percent/chromium/product_logo_$size.png"
  png "$HERE/oblada-tile.svg" $((size * 2)) "$THEME/default_200_percent/chromium/product_logo_$size.png"
done
cp "$HERE/oblada-tile.svg" "$THEME/chromium/product_logo.svg"
cp "$HERE/oblada-tile.svg" "$THEME/chromium/product_logo_animation.svg"

# The menu bar icon, which macOS draws in one color.
sed 's/currentColor/#000/' "$HERE/oblada-glyph.svg" > "$WORK/mono.svg"
png "$WORK/mono.svg" 22 "$THEME/chromium/product_logo_22_mono.png"

# Where Chromium draws its product icon in one color, as a vector (the address bar's chip on
# internal pages, menus, notifications): the same glyph, at every path it keeps one.
OVERRIDES="$HERE/../chromium/overrides"
python3 "$HERE/vector_icon.py" "$WORK/product.icon"
for icon in components/omnibox/browser/vector_icons/chrome_product.icon \
            components/omnibox/browser/vector_icons/product_chrome_refresh_old.icon \
            components/omnibox/browser/vector_icons/product_old.icon \
            components/vector_icons/chromium/product.icon \
            components/vector_icons/chromium/product_refresh.icon \
            chrome/app/vector_icons/chrome_product.icon \
            ui/message_center/vector_icons/chrome_product.icon \
            ui/message_center/vector_icons/product_old.icon; do
  mkdir -p "$(dirname "$OVERRIDES/$icon")"
  cp "$WORK/product.icon" "$OVERRIDES/$icon"
done

# The internal pages' logo in the dark theme (in the light one, product_logo_32), and the
# New Tab page's favicon.
mkdir -p "$OVERRIDES/ui/webui/resources/images"
cp "$HERE/oblada-tile.svg" "$OVERRIDES/ui/webui/resources/images/chrome_logo_dark.svg"
for size in 16 32; do
  dir=$([ "$size" = 16 ] && echo default_100_percent || echo default_200_percent)
  png "$HERE/oblada-tile.svg" "$size" "$THEME/$dir/common/favicon_ntp.png"
done

# The logo with the name next to it, which payment dialogs show (dark text, and white).
for variant in "#1f1f1f:" "#ffffff:_white"; do
  color="${variant%%:*}"; suffix="${variant#*:}"
  python3 - "$HERE/oblada-tile.svg" "$color" > "$WORK/name$suffix.svg" <<'PY'
import re, sys
tile = open(sys.argv[1]).read()
inner = re.sub(r'^\s*<svg[^>]*>|</svg>\s*$', '', tile.strip())
print(f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 97 22" width="97" height="22">
  <svg x="0" y="0" width="22" height="22" viewBox="100 100 824 824">{inner}</svg>
  <text x="28" y="16" font-family="Helvetica Neue" font-size="15" font-weight="500"
        fill="{sys.argv[2]}">Oblada</text>
</svg>''')
PY
  png "$WORK/name$suffix.svg" 97 "$THEME/default_100_percent/chromium/product_logo_name_22$suffix.png"
  png "$WORK/name$suffix.svg" 194 "$THEME/default_200_percent/chromium/product_logo_name_22$suffix.png"
  # The same, 32 points high, at the top of oblada://version.
  png "$WORK/name$suffix.svg" 141 "$OVERRIDES/components/resources/default_100_percent/chromium/product_logo$suffix.png"
  png "$WORK/name$suffix.svg" 282 "$OVERRIDES/components/resources/default_200_percent/chromium/product_logo$suffix.png"
done
