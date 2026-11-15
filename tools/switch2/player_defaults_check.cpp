// Host-only checks, no console, settings file or game data required.
#include "platform/switch2_player_defaults.h"
#include "overlay/release_gate.h"
#include <cassert>

int main() {
    using switch2::player::SettingsChord;
    using switch2::player::presentation_defaults;
    SettingsChord chord;
    assert(!chord.poll(true, true, true));  // held at startup is not an action
    assert(!chord.poll(false, false, false));
    assert(!chord.poll(false, false, true));  // Minus alone
    assert(!chord.poll(true, false, true));
    assert(chord.poll(true, true, true));
    for (int i = 0; i < 100; ++i) assert(!chord.poll(true, true, true));
    assert(!chord.poll(true, true, false));
    assert(!chord.poll(true, true, true));  // must release shoulders too
    assert(!chord.poll(false, false, false));
    assert(chord.poll(true, true, true));  // same chord also closes the menu

    overlay::ReleaseGate gate;
    overlay::PadValues raw{};
    raw[input_map::kPadLB] = raw[input_map::kPadRB] = raw[input_map::kPadOptions] = 1;
    auto game = raw;
    gate.filter(game.data(), true);  // opening or closing poll takes the chord
    for (float value : game) assert(value == 0);
    game = raw;
    gate.filter(game.data(), false);
    for (float value : game) assert(value == 0);  // no held buttons leak on close
    game = {};
    gate.filter(game.data(), false);
    game[input_map::kPadOptions] = 1;
    gate.filter(game.data(), false);
    assert(game[input_map::kPadOptions] == 1);  // a new Minus belongs to the game

    std::map<std::string, std::string> settings{{"fps60", "0"}, {"audioSpeakers", "stereo"}};
    assert(presentation_defaults(settings, false));
    assert(settings.at("fps60") == "0" && settings.at("interpFps") == "60");
    assert(settings.at("fps60Paced") == "1" && settings.at("audioSpeakers") == "stereo");
    settings["fps60"] = "1";  // later player choice must win on the next boot
    assert(!presentation_defaults(settings, false));
    assert(settings.at("fps60") == "1");
    std::map<std::string, std::string> overridden;
    assert(!presentation_defaults(overridden, true));
    assert(overridden.empty());
    assert(presentation_defaults(overridden, false));
    assert(overridden.at("fps60") == "0");
    std::map<std::string, std::string> existing{{"fps60", "2"}, {"interpFps", "120"}, {"fps60Paced", "0"}};
    assert(presentation_defaults(existing, false));
    assert(existing.at("fps60") == "2" && existing.at("interpFps") == "120" && existing.at("fps60Paced") == "0");
}
