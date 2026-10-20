# dmgbuild's settings for Oblada's DMG (see dmg.sh): the app and a link to Applications,
# where branding/dmg's background leaves room for them.
# defines: app (the app to pack), background (background.png; background@2x.png next to
# it is used on Retina screens).

app = defines["app"]  # noqa: F821 (set by dmgbuild)
background = defines["background"]  # noqa: F821
format = "ULFO"
filesystem = "HFS+"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = app + "/Contents/Resources/app.icns"

window_rect = ((200, 140), (660, 420))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False
arrange_by = None
label_pos = "bottom"
text_size = 13
icon_size = 128
icon_locations = {"Oblada.app": (165, 220), "Applications": (495, 220)}
# No hide_extensions: the Finder info it sets on the app fails its signature's strict
# check.
