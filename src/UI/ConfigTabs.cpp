#include "UI/ConfigTabs.h"
#include "Config/Config.h"
#include "STL.h"
#include "Skin/Distribution.h"
#include "Preview/Preview.h"
#include "SKSEMenuFramework.h"
#include "UI/JsonView.h"
#include "UI/KeyConflicts.h"
#include "UI/Lookups.h"
#include "UI/PreviewPanel.h"
#include "UI/Translations.h"
#include "UI/UI.h"
#include "UI/Util.h"
#include "UI/Widgets.h"

namespace {
    constexpr ImGuiMCP::ImVec4 kSuccessText{0.55f, 0.85f, 0.55f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kErrorText{1.0f, 0.45f, 0.4f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kWarningText{0.95f, 0.7f, 0.35f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kHintText{0.7f, 0.7f, 0.75f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kGoldText{0.96f, 0.84f, 0.58f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kKeyFill{0.19f, 0.18f, 0.17f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kKeyFillHovered{0.25f, 0.23f, 0.20f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kKeyBorder{0.55f, 0.46f, 0.30f, 1.0f};

    constexpr auto ShowBlacklistedKey{"blacklistedSkinsShowInMenu"};
    constexpr auto PlayerRulesKey{"playerFollowsRules"};
    constexpr auto DisableDistributionKey{"disableSkinDistribution"};

    constexpr std::array<std::pair<std::string_view, const char*>, 4> DistributionValues{{
        {"none"sv, "oskin_settings_distribution_to_all"},
        {"male"sv, "oskin_settings_distribution_to_female"},
        {"female"sv, "oskin_settings_distribution_to_male"},
        {"all"sv, "oskin_settings_distribution_to_none"},
    }};

    enum class Tab { kNone, kSettings, kIncludeFiles, kViewer };

    std::string g_result;
    bool g_failed{false};
    Tab g_resultTab{Tab::kNone};

    void TextColored(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, a_color);
        ImGuiMCP::TextWrapped("%s", a_text.c_str());
        ImGuiMCP::PopStyleColor();
    }

    void VerticalSpace(const float a_lines) {
        ImGuiMCP::Dummy(ImGuiMCP::ImVec2{0.0f, ImGuiMCP::GetTextLineHeight() * a_lines});
    }

    float g_controlColumn{0.0f};

    float ControlColumn() {
        float widest{0.0f};

        for (const auto* const key : {"oskin_settings_distribution", "oskin_settings_menu_key"}) {
            widest = (std::max)(widest, ImGuiMCP::CalcTextSize(UI::Translations::Get(key).c_str()).x);
        }

        return widest + ImGuiMCP::GetFontSize() * 1.5f;
    }

    float ComboWidth() { return ImGuiMCP::GetFontSize() * 11.0f; }

    void SettingLabel(const char* a_key) {
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted(UI::Translations::Get(a_key).c_str());
        ImGuiMCP::SameLine(g_controlColumn);
    }

    void SettingNote(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        UI::Widgets::SetTextScale(0.92f);
        TextColored(a_color, a_text);
        UI::Widgets::SetTextScale();
    }

    void SettingHint(const char* a_key) { SettingNote(kHintText, UI::Translations::Get(a_key)); }

    std::string Translate(const char* a_key, std::initializer_list<std::string_view> a_values) {
        auto text{UI::Translations::Get(a_key)};

        for (const auto value : a_values) {
            if (const auto at{text.find("{}")}; at != std::string::npos) {
                text.replace(at, 2, value);
            }
        }

        return text;
    }

    void Reload(const Tab a_tab, const std::string& a_prefix) {
        const auto result{UI::ConfigTabs::Reload()};

        g_resultTab = a_tab;
        g_failed = result.failed;
        g_result = a_prefix + result.message;
    }

    void OnSettingSaved(const std::optional<std::string>& a_error) {
        if (a_error) {
            g_resultTab = Tab::kSettings;
            g_failed = true;
            g_result = UI::Translations::Get("oskin_settings_save_failed") + " " + *a_error;
            return;
        }

        // The setting shows its new value, so only a problem is told
        const auto result{UI::ConfigTabs::Reload()};

        g_resultTab = Tab::kSettings;
        g_failed = result.failed;
        g_result = result.failed || result.filesSkipped ? result.message : std::string{};
    }

    void RenderResult(const Tab a_tab) {
        if (g_result.empty() || g_resultTab != a_tab) return;

        VerticalSpace(0.3f);
        TextColored(g_failed ? kErrorText : kSuccessText, g_result);
    }

    void RenderReloadButton(const Tab a_tab) {
        if (ImGuiMCP::Button(UI::Translations::Get("oskin_reload_config").c_str())) {
            Reload(a_tab, "");
        }

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip("%s", UI::Translations::Get("oskin_reload_config_tooltip").c_str());
        }

        ImGuiMCP::SameLine();
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kHintText);
        ImGuiMCP::TextUnformatted(UI::Translations::Get("oskin_reload_config_hint").c_str());
        ImGuiMCP::PopStyleColor();

        RenderResult(a_tab);
    }

    void RenderDistributionSetting(const rapidjson::Document& a_document) {
        const auto distributionItr{a_document.FindMember(DisableDistributionKey)};
        const std::string_view distribution{
            distributionItr != a_document.MemberEnd() && distributionItr->value.IsString()
                ? std::string_view{distributionItr->value.GetString(), distributionItr->value.GetStringLength()}
                : "none"sv};
        const auto current{
            std::ranges::find(DistributionValues, distribution, &std::pair<std::string_view, const char*>::first)};
        // A value the config doesn't know is shown as it's written
        const std::string currentName{current != DistributionValues.end() ? UI::Translations::Get(current->second)
                                                                          : std::string{distribution}};

        SettingLabel("oskin_settings_distribution");
        ImGuiMCP::SetNextItemWidth(ComboWidth());

        if (UI::Widgets::BeginCombo("##OSkinDistribution", currentName.c_str())) {
            for (const auto& [value, nameKey] : DistributionValues) {
                const bool selected{value == distribution};

                if (ImGuiMCP::Selectable(UI::Translations::Get(nameKey).c_str(), selected) && !selected) {
                    OnSettingSaved(Parser::SetMasterConfigValue(DisableDistributionKey, value));
                }

                if (selected) {
                    ImGuiMCP::SetItemDefaultFocus();
                }
            }
            ImGuiMCP::EndCombo();
        }

        SettingHint("oskin_settings_distribution_hint");
    }

    constexpr std::uint32_t kEscape{0x01};
    constexpr std::uint32_t kBackspace{0x0E};

    std::optional<std::uint32_t> g_checkedKey;
    std::string g_keyUser;
    std::string g_keyRefused;

    void RenderHotkeySetting(const Parser::Config& a_config) {
        if (const auto captured{UI::Menu::TakeCapturedHotkey()}) {
            if (*captured == kBackspace) {
                OnSettingSaved(Parser::SetMasterConfigNumber("menuHotkey", 0));
            } else if (*captured != kEscape) {
                if (const auto user{UI::KeyConflicts::FindUser(*captured)}; !user.empty()) {
                    g_keyRefused = Translate("oskin_settings_key_taken", {UI::Menu::GetKeyName(*captured), user});
                    logger::info("The menu key {} wasn't set, {} uses it", *captured, user);
                } else {
                    OnSettingSaved(Parser::SetMasterConfigNumber("menuHotkey", *captured));
                }
            }
        }

        const auto key{a_config.GetMenuHotkey()};

        if (g_checkedKey != key) {
            g_checkedKey = key;
            g_keyUser = UI::KeyConflicts::FindUser(key);
        }

        const bool capturing{UI::Menu::IsCapturingHotkey()};
        const auto shown{capturing  ? UI::Translations::Get("oskin_settings_press_key")
                         : key == 0 ? UI::Translations::Get("oskin_settings_no_key")
                                    : UI::Menu::GetKeyName(key)};

        SettingLabel("oskin_settings_menu_key");

        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, kKeyFill);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, kKeyFillHovered);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, kKeyFillHovered);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Border, capturing ? kGoldText : kKeyBorder);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, key == 0 && !capturing ? kHintText : kGoldText);

        if (ImGuiMCP::Button((shown + "##OSkinMenuKey").c_str(), ImGuiMCP::ImVec2{ComboWidth(), 0.0f}) && !capturing) {
            g_keyRefused.clear();
            UI::Menu::BeginHotkeyCapture();
        }

        ImGuiMCP::PopStyleColor(5);
        ImGuiMCP::PopStyleVar();

        if (capturing) {
            SettingHint("oskin_settings_key_capture_hint");
        } else if (!g_keyRefused.empty()) {
            SettingNote(kWarningText, g_keyRefused);
        } else if (!g_keyUser.empty()) {
            SettingNote(kWarningText, Translate("oskin_settings_key_shared", {UI::Menu::GetKeyName(key), g_keyUser}));
        } else {
            SettingHint(key == 0 ? "oskin_settings_menu_key_hint" : "oskin_settings_key_change_hint");
        }
    }

    bool g_confirmReroll{false};
    std::string g_rerollResult;

    std::string WithCount(const char* a_key, const std::size_t a_count) {
        auto text{UI::Translations::Get(a_key)};

        if (const auto at{text.find("{}")}; at != std::string::npos) {
            text.replace(at, 2, std::to_string(a_count));
        }

        return text;
    }

    std::vector<RE::Actor*> GetLoadedRerollTargets() {
        std::vector<RE::Actor*> actors;
        auto& preview{Preview::Session::GetInstance()};

        if (auto* const processLists{RE::ProcessLists::GetSingleton()}) {
            for (const auto& handle : processLists->highActorHandles) {
                const auto actor{handle.get()};

                if (!actor || !actor->Is3DLoaded() || actor->IsPlayerRef() || preview.IsCopy(actor.get()) ||
                    !UI::Util::IsSkinTarget(actor.get()) || Skin::Distribution::IsPickedByHand(actor.get())) {
                    continue;
                }

                actors.push_back(actor.get());
            }
        }

        return actors;
    }

    void SectionHeader(const std::string& a_text) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kGoldText);
        UI::Widgets::SetTextScale(1.15f);
        ImGuiMCP::TextUnformatted(a_text.c_str());
        UI::Widgets::SetTextScale();
        ImGuiMCP::PopStyleColor();
        ImGuiMCP::Separator();
        VerticalSpace(0.4f);
    }

    bool AccentButton(const std::string& a_label, const float a_width) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, ImGuiMCP::ImVec4{0.27f, 0.23f, 0.16f, 1.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, ImGuiMCP::ImVec4{0.35f, 0.29f, 0.19f, 1.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, ImGuiMCP::ImVec4{0.42f, 0.35f, 0.23f, 1.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kGoldText);
        ImGuiMCP::PushStyleVar(
            ImGuiMCP::ImGuiStyleVar_FramePadding,
            ImGuiMCP::ImVec2{ImGuiMCP::GetStyle()->FramePadding.x * 2.0f, ImGuiMCP::GetTextLineHeight() * 0.45f});

        const bool clicked{ImGuiMCP::Button(a_label.c_str(), ImGuiMCP::ImVec2{a_width, 0.0f})};

        ImGuiMCP::PopStyleVar();
        ImGuiMCP::PopStyleColor(4);

        return clicked;
    }

    void RenderRerollAll() {
        const float width{ImGuiMCP::GetContentRegionAvail().x * 0.6f};
        const float centered{(ImGuiMCP::GetWindowSize().x - width) * 0.5f};

        SectionHeader(UI::Translations::Get("oskin_settings_section_reroll"));
        TextColored(kHintText, UI::Translations::Get("oskin_reroll_all_hint"));
        VerticalSpace(0.5f);

        if (!g_confirmReroll) {
            ImGuiMCP::SetCursorPosX(centered);

            if (AccentButton(UI::Translations::Get("oskin_reroll_all_button"), width)) {
                g_confirmReroll = true;
                g_rerollResult.clear();
            }

            if (!g_rerollResult.empty()) {
                VerticalSpace(0.5f);
                TextColored(kSuccessText, g_rerollResult);
            }

            return;
        }

        const auto targets{GetLoadedRerollTargets()};

        TextColored(kWarningText, WithCount("oskin_reroll_all_confirm", targets.size()));
        VerticalSpace(0.5f);

        const float buttonWidth{(width - ImGuiMCP::GetStyle()->ItemSpacing.x * 3.0f) * 0.5f};
        ImGuiMCP::SetCursorPosX(centered);

        if (AccentButton(UI::Translations::Get("oskin_reroll_all_yes"), buttonWidth)) {
            g_confirmReroll = false;

            // The preview would put the NPC's old skin back as it ends
            UI::PreviewPanel::End();

            const auto rerolled{Skin::Distribution::RerollAll(targets)};
            g_rerollResult = WithCount("oskin_reroll_all_done", rerolled);
        }

        ImGuiMCP::SameLine(0.0f, ImGuiMCP::GetStyle()->ItemSpacing.x * 3.0f);
        ImGuiMCP::PushStyleVar(
            ImGuiMCP::ImGuiStyleVar_FramePadding,
            ImGuiMCP::ImVec2{ImGuiMCP::GetStyle()->FramePadding.x * 2.0f, ImGuiMCP::GetTextLineHeight() * 0.45f});

        if (ImGuiMCP::Button(UI::Translations::Get("oskin_reroll_all_cancel").c_str(),
                             ImGuiMCP::ImVec2{buttonWidth, 0.0f})) {
            g_confirmReroll = false;
        }

        ImGuiMCP::PopStyleVar();
    }

    void RenderIncludeFileTable(const std::vector<Parser::IncludeFileInfo>& a_files, const std::size_t a_inGame) {
        constexpr auto flags{ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                             ImGuiMCP::ImGuiTableFlags_BordersOuterH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp};

        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, ImGuiMCP::ImVec2{8.0f, 5.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_TableHeaderBg, UI::Widgets::TableHeaderBackground);

        if (!ImGuiMCP::BeginTable("##OSkinIncludeFiles", 3, flags)) {
            ImGuiMCP::PopStyleColor();
            ImGuiMCP::PopStyleVar();
            return;
        }

        ImGuiMCP::TableSetupColumn("#", 0, 0.3f);
        ImGuiMCP::TableSetupColumn(UI::Translations::Get("oskin_include_files_column_file").c_str(), 0, 3.0f);
        ImGuiMCP::TableSetupColumn(UI::Translations::Get("oskin_include_files_column_folder").c_str(), 0, 5.0f);

        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, UI::Widgets::TableHeaderText);
        ImGuiMCP::TableHeadersRow();
        ImGuiMCP::PopStyleColor();

        for (std::size_t i{0}; i < a_files.size(); ++i) {
            const auto& file{a_files[i]};
            const auto separator{file.from.find_last_of("\\/")};
            const std::string folder{separator == std::string::npos ? file.from : file.from.substr(0, separator)};

            ImGuiMCP::TableNextRow();

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%zu", i + 1);

            ImGuiMCP::TableNextColumn();

            if (file.skipped) {
                TextColored(kErrorText, file.name + " " + UI::Translations::Get("oskin_include_files_skipped"));
            } else if (i == a_inGame) {
                TextColored(kGoldText, file.name);
            } else {
                ImGuiMCP::TextUnformatted(file.name.c_str());
            }

            ImGuiMCP::TableNextColumn();
            ImGuiMCP::TextUnformatted(folder.c_str());

            if (ImGuiMCP::IsItemHovered()) {
                ImGuiMCP::SetTooltip("%s", file.from.c_str());
            }
        }

        ImGuiMCP::EndTable();
        ImGuiMCP::PopStyleColor();
        ImGuiMCP::PopStyleVar();
    }
}  // namespace

void UI::ConfigTabs::RenderSettings() {
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    const auto& document{config->document};
    const auto showItr{document.FindMember(ShowBlacklistedKey)};
    // On when not set, like the Skins tab reads it
    bool show{showItr == document.MemberEnd() || !showItr->value.IsBool() || showItr->value.GetBool()};

    g_controlColumn = ControlColumn();

    // Only a problem shows, at the top so it's seen
    RenderResult(Tab::kSettings);
    VerticalSpace(0.5f);

    SectionHeader(UI::Translations::Get("oskin_settings_section_config"));
    RenderDistributionSetting(document);
    VerticalSpace(0.6f);

    if (bool follows{config->DoesPlayerFollowRules()};
        UI::Widgets::Checkbox(UI::Translations::Get("oskin_settings_player_rules").c_str(), &follows)) {
        OnSettingSaved(Parser::SetMasterConfigValue(PlayerRulesKey, follows));

        // The player's skin follows the new setting at once, unless it was picked in the menu
        if (auto* const player{RE::PlayerCharacter::GetSingleton()};
            player && !Skin::Distribution::IsPickedByHand(player)) {
            Skin::Distribution::Reroll(player);
        }
    }

    SettingHint("oskin_settings_player_rules_hint");
    VerticalSpace(0.6f);

    if (UI::Widgets::Checkbox(UI::Translations::Get("oskin_settings_show_blacklisted").c_str(), &show)) {
        OnSettingSaved(Parser::SetMasterConfigValue(ShowBlacklistedKey, show));
    }

    SettingHint("oskin_settings_show_blacklisted_hint");

    VerticalSpace(1.4f);
    SectionHeader(UI::Translations::Get("oskin_settings_section_menu"));
    RenderHotkeySetting(*config);

    VerticalSpace(1.4f);
    RenderRerollAll();

    VerticalSpace(1.4f);
    UI::Widgets::SetTextScale(0.92f);
    TextColored(kHintText, UI::Translations::Get("oskin_settings_master_note"));
    UI::Widgets::SetTextScale();
}

void UI::ConfigTabs::RenderIncludeFiles() {
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    const auto& files{config->includeFiles};
    const auto inGame{static_cast<std::size_t>(
        std::ranges::find_if(files,
                             [](const auto& a_file) { return stl::cmp(a_file.name, Parser::InGameConfigFileName); }) -
        files.begin())};

    VerticalSpace(0.3f);
    RenderReloadButton(Tab::kIncludeFiles);
    VerticalSpace(0.8f);

    TextColored(kHintText, UI::Translations::Get("oskin_include_files_rule"));
    VerticalSpace(0.6f);
    TextColored(kGoldText, UI::Translations::Get("oskin_include_files_ingame") + " " + Parser::InGameConfigFileName);

    TextColored(kHintText, UI::Translations::Get(inGame == files.size() ? "oskin_include_files_ingame_missing"
                                                                        : "oskin_include_files_ingame_last"));

    VerticalSpace(0.8f);

    if (files.empty()) {
        TextColored(kHintText, UI::Translations::Get("oskin_include_files_none"));
    } else {
        RenderIncludeFileTable(files, inGame);
    }
}

void UI::ConfigTabs::RenderViewer() {
    VerticalSpace(0.3f);
    RenderReloadButton(Tab::kViewer);
    VerticalSpace(0.8f);
    TextColored(kHintText, UI::Translations::Get("oskin_config_tree_header"));
    VerticalSpace(0.3f);

    // Taken after the reload button, which can replace the config in use
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    UI::JsonView::Render(config->document);
}

void UI::ConfigTabs::Reset() {
    g_result.clear();
    UI::Menu::CancelHotkeyCapture();
    g_checkedKey.reset();
    g_keyRefused.clear();
    g_resultTab = Tab::kNone;
    g_confirmReroll = false;
    g_rerollResult.clear();
}

UI::ConfigTabs::ReloadResult UI::ConfigTabs::Reload() {
    const auto loaded{Parser::JSONParser::GetInstance().Load()};
    ReloadResult result{.failed = loaded.error.has_value(),
                        .filesSkipped = !loaded.error && !loaded.skippedFiles.empty()};

    result.message = loaded.error ? UI::Translations::Get("oskin_reload_config_failed") + " " + *loaded.error
                                  : UI::Translations::Get("oskin_reload_config_done");

    if (!loaded.error && !loaded.skippedFiles.empty()) {
        result.message += "\n" + UI::Translations::Get("oskin_reload_config_skipped");

        for (const auto& file : loaded.skippedFiles) {
            result.message += "\n" + file;
        }
    }

    if (!loaded.error) {
        UI::Lookups::RebuildSkins();
        UI::Menu::SetHotkeyScanCode(Parser::JSONParser::GetInstance().GetConfig()->GetMenuHotkey());

        // The rules are worked out each time a skin is applied, so a change to them shows right away
        Skin::Distribution::ApplyToLoadedActors("Config reloaded");
    }

    return result;
}
