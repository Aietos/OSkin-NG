#include "UI/KeyConflicts.h"

#include "STL.h"
#include "UI/Translations.h"
#include "UI/UI.h"

namespace {
    std::string_view Trimmed(std::string_view a_text) {
        while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) a_text.remove_prefix(1);
        while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) a_text.remove_suffix(1);

        return a_text;
    }

    std::string Lowered(const std::string_view a_text) {
        std::string lowered{a_text};
        std::ranges::transform(lowered, lowered.begin(),
                               [](const unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });

        return lowered;
    }

    void ReadIni(const std::filesystem::path& a_path,
                 const std::function<void(std::string_view, std::string_view, std::string_view)>& a_line) {
        std::ifstream file{a_path};
        std::string section;

        for (std::string line; std::getline(file, line);) {
            auto text{Trimmed(line)};

            if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);

            if (text.empty() || text.front() == ';' || text.front() == '#') continue;

            if (text.front() == '[') {
                section = Lowered(Trimmed(text.substr(1, text.find(']') - 1)));
                continue;
            }

            if (const auto equals{text.find('=')}; equals != std::string_view::npos) {
                a_line(section, Lowered(Trimmed(text.substr(0, equals))), Trimmed(text.substr(equals + 1)));
            }
        }
    }

    // MCM settings name their whole numbers with an i, keys have "key" in the name
    std::optional<std::uint32_t> ParseKey(const std::string_view a_name, const std::string_view a_value) {
        if (!a_name.starts_with('i') || !a_name.contains("key")) return std::nullopt;

        std::uint32_t key{0};
        const auto [end, error]{std::from_chars(a_value.data(), a_value.data() + a_value.size(), key)};

        if (error != std::errc{} || end != a_value.data() + a_value.size() || key == 0 || key > 0xFF) {
            return std::nullopt;
        }

        return key;
    }

    // A mod whose MCM settings give it the key. Its defaults are in MCM\Config\<mod>\settings.ini, the player's
    // changes in MCM\Settings\<mod>.ini
    std::string FindMod(const std::uint32_t a_scanCode) {
        std::map<std::string, std::pair<std::string, std::map<std::string, std::uint32_t>>> mods;
        std::error_code error;

        const auto read = [&](const std::filesystem::path& a_path, const std::string& a_mod) {
            auto& [name, keys]{mods[Lowered(a_mod)]};
            name = a_mod;

            ReadIni(a_path, [&](const std::string_view a_section, const std::string_view a_name,
                                const std::string_view a_value) {
                const auto id{std::format("{}|{}", a_section, a_name)};

                if (const auto key{ParseKey(a_name, a_value)}) {
                    keys.insert_or_assign(id, *key);
                } else {
                    // The player's settings can turn a default key off
                    keys.erase(id);
                }
            });
        };

        for (const auto& entry : std::filesystem::directory_iterator{"Data/MCM/Config", error}) {
            if (const auto settings{entry.path() / "settings.ini"}; std::filesystem::exists(settings, error)) {
                read(settings, entry.path().filename().string());
            }
        }

        for (const auto& entry : std::filesystem::directory_iterator{"Data/MCM/Settings", error}) {
            if (stl::cmp(entry.path().extension().string(), ".ini")) {
                read(entry.path(), entry.path().stem().string());
            }
        }

        for (const auto& [name, keys] : mods | std::views::values) {
            if (std::ranges::contains(keys | std::views::values, a_scanCode)) return name;
        }

        return {};
    }

    bool IsFrameworkToggle(const std::uint32_t a_scanCode) {
        std::string key;
        std::string mode;

        ReadIni("Data/SKSE/Plugins/SKSEMenuFramework.ini",
                [&](const std::string_view a_section, const std::string_view a_name, const std::string_view a_value) {
                    if (a_section != "general") return;

                    if (a_name == "togglekey") {
                        key = a_value;
                    } else if (a_name == "togglemode") {
                        mode = Lowered(a_value);
                    }
                });

        return !key.empty() && mode != "off" && stl::cmp(UI::Menu::GetKeyName(a_scanCode), key);
    }
}  // namespace

std::string UI::KeyConflicts::FindUser(const std::uint32_t a_scanCode) {
    if (a_scanCode == 0) return {};

    if (const auto* const controls{RE::ControlMap::GetSingleton()}) {
        if (const auto event{controls->GetUserEventName(a_scanCode, RE::INPUT_DEVICE::kKeyboard)}; !event.empty()) {
            auto text{UI::Translations::Get("oskin_key_user_game")};

            if (const auto at{text.find("{}")}; at != std::string::npos) {
                text.replace(at, 2, event);
            }

            return text;
        }
    }

    if (IsFrameworkToggle(a_scanCode)) return UI::Translations::Get("oskin_key_user_framework");

    return FindMod(a_scanCode);
}
