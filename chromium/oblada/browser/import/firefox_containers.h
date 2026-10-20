#ifndef OBLADA_BROWSER_IMPORT_FIREFOX_CONTAINERS_H_
#define OBLADA_BROWSER_IMPORT_FIREFOX_CONTAINERS_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"

class Profile;

namespace oblada::import {

// A container of Firefox's, as its profile's containers.json has
// it. Firefox's own four have no name of their own but a string's id: they are
// matched with Oblada's own four (`default_id`, e.g. "work").
struct FirefoxContainer {
  std::string name;
  std::string default_id;
  std::string color;
  std::string icon;
};

// The profile's containers, but those Firefox keeps for itself. Blocks.
std::vector<FirefoxContainer> ReadFirefoxContainers(
    const base::FilePath& profile_dir);

// Gives `profile` the containers: one Oblada already has (by name, or as one of
// the four) takes their color and icon, the others are added. Returns how many.
int AddFirefoxContainers(Profile* profile,
                         const std::vector<FirefoxContainer>& firefox_ones);

}  // namespace oblada::import

#endif  // OBLADA_BROWSER_IMPORT_FIREFOX_CONTAINERS_H_
