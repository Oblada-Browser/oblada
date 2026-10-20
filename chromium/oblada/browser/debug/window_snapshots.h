#ifndef OBLADA_BROWSER_DEBUG_WINDOW_SNAPSHOTS_H_
#define OBLADA_BROWSER_DEBUG_WINDOW_SNAPSHOTS_H_

namespace oblada::debug {

// With --oblada-snapshot-dir=<dir>, whenever a file named "request" appears in <dir>,
// writes the active browser window (browser UI included, which DevTools screenshots
// leave out) to <dir>/window.png and deletes the request. It reads the window's own
// compositor, so it works headless: the UI can be checked without showing a window.
void MaybeStartWindowSnapshots();

}  // namespace oblada::debug

#endif  // OBLADA_BROWSER_DEBUG_WINDOW_SNAPSHOTS_H_
