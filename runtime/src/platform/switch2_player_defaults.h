#pragma once
#include <map>
#include <string>

namespace switch2::player {
// All three buttons must be released after startup and after each activation.
// Releasing/repressing only Minus while L/R remain held must not toggle again.
class SettingsChord {
    bool armed_ = false;
public:
    bool poll(bool left, bool right, bool minus) {
        if (!left && !right && !minus) armed_ = true;
        if (!armed_ || !left || !right || !minus) return false;
        armed_ = false;
        return true;
    }
};

// Native 30 fps on fresh installations. Explicit saved player choices and
// environment overrides survive; changing the default never rewrites them.
inline bool presentation_defaults(std::map<std::string, std::string>& settings, bool environment_override) {
    if (environment_override || settings.count("switch2PresentationDefaults")) return false;
    settings.emplace("fps60", "0");  // no interpolation or True 60; original 30 Hz game logic
    settings.emplace("interpFps", "60");  // remembered rate only if interpolation is later enabled
    settings.emplace("fps60Paced", "1");
    settings["switch2PresentationDefaults"] = "1";
    return true;
}
}  // namespace switch2::player
