#ifndef OBLADA_BROWSER_WORKSPACES_THEME_PICKER_H_
#define OBLADA_BROWSER_WORKSPACES_THEME_PICKER_H_

#include <string>

class BrowserWindowInterface;

namespace ui {
class SimpleMenuModel;
}  // namespace ui

namespace views {
class View;
}  // namespace views

namespace oblada::workspaces {

// Opens a bubble next to `anchor` to change the theme of workspace `id`
// (workspace_theme.h), as Zen's and Arc's color pickers do:
// - at the top, whether the window is light, dark, or as the browser is;
// - a field to place up to three colors on, the hue around its middle and the
//   saturation away from it, with buttons to add one, take one away and change
//   how they go together;
// - colors ready to pick, a page of eight at a time;
// - how much the colors cover the window's glass (a wave), and grain (a dial).
// Changes apply as they're made; the bubble goes once the user clicks elsewhere. A
// workspace that has the default theme gets its own as it's changed.
void ShowThemePicker(BrowserWindowInterface* browser,
                     const std::string& id,
                     views::View* anchor);

// The same for the default theme, that of the workspaces without their own, next
// to the sidebar.
void ShowDefaultThemePicker(BrowserWindowInterface* browser);

// The sidebar's menu (a right click on it opens the window's system menu) edits and
// resets the default theme.
inline constexpr int kEditDefaultThemeCommandId = 55660;
inline constexpr int kResetDefaultThemeCommandId = 55661;
void AddDefaultThemeItems(ui::SimpleMenuModel* model);
// Whether `command_id` is one of those, which the functions below handle.
bool IsDefaultThemeCommand(int command_id);
bool IsDefaultThemeCommandEnabled(BrowserWindowInterface* browser, int command_id);
void ExecuteDefaultThemeCommand(BrowserWindowInterface* browser, int command_id);

}  // namespace oblada::workspaces

#endif  // OBLADA_BROWSER_WORKSPACES_THEME_PICKER_H_
