#include "UI/SkinsTab.h"

#include "API/Links.h"
#include "Config/Config.h"
#include "SKSEMenuFramework.h"
#include "STL.h"
#include "Skin/Body.h"
#include "Skin/Distribution.h"
#include "UI/ConfigEditor.h"
#include "UI/ConfigTabs.h"
#include "UI/PreviewPanel.h"
#include "UI/Translations.h"
#include "UI/UI.h"
#include "UI/Util.h"
#include "UI/Widgets.h"

namespace {
    char g_search[128] = "";

    // A skin is previewed once the mouse rests on it, so moving across the list doesn't change what's shown
    constexpr auto kHoverRest{std::chrono::milliseconds{250}};
    constexpr float kHoverStillDistance{6.0f};

    struct HoverRest {
        std::string name;
        ImGuiMCP::ImVec2 anchor;
        std::chrono::steady_clock::time_point since;
        // Once per rest, so the arrow keys can move on from it
        bool shown{false};
    };
    HoverRest g_hoverRest;

    std::string g_lastFocused;

    void CenteredText(const char* text, float fontScale = 1.0f) {
        const float windowWidth = ImGuiMCP::GetWindowSize().x;
        UI::Widgets::SetTextScale(fontScale);
        const float textWidth = ImGuiMCP::CalcTextSize(text).x;
        ImGuiMCP::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGuiMCP::Text("%s", text);
        UI::Widgets::SetTextScale();
    }

    void CenteredLoadingText(const std::string& a_text) {
        const float size{ImGuiMCP::GetTextLineHeight()};
        const float gap{ImGuiMCP::GetStyle()->ItemSpacing.x};
        const float width{ImGuiMCP::CalcTextSize(a_text.c_str()).x + gap + size};

        ImGuiMCP::SetCursorPosX((ImGuiMCP::GetWindowSize().x - width) * 0.5f);
        ImGuiMCP::TextUnformatted(a_text.c_str());
        ImGuiMCP::SameLine(0.0f, gap);

        const auto corner{ImGuiMCP::GetCursorScreenPos()};
        const ImGuiMCP::ImVec2 center{corner.x + size * 0.5f, corner.y + size * 0.5f};
        const float radius{size * 0.35f};
        const auto turn{static_cast<float>(ImGuiMCP::GetTime()) * 6.0f};
        auto* const drawList{ImGuiMCP::GetWindowDrawList()};

        ImGuiMCP::ImDrawListManager::PathArcTo(drawList, center, radius, turn, turn + 4.2f, 24);
        ImGuiMCP::ImDrawListManager::PathStroke(drawList, ImGuiMCP::GetColorU32(ImGuiMCP::ImGuiCol_Text),
                                                ImGuiMCP::ImDrawFlags_None, size * 0.12f);
        ImGuiMCP::Dummy(ImGuiMCP::ImVec2{size, size});
    }

    void VerticalSpace(const float a_lines) {
        ImGuiMCP::Dummy(ImGuiMCP::ImVec2{0.0f, ImGuiMCP::GetTextLineHeight() * a_lines});
    }

    void CenteredWrappedText(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        const float windowWidth{ImGuiMCP::GetWindowSize().x};
        const float maxWidth{windowWidth - ImGuiMCP::GetStyle()->WindowPadding.x * 2.0f};
        const auto width = [](const std::string& a_line) { return ImGuiMCP::CalcTextSize(a_line.c_str()).x; };
        std::vector<std::string> lines;
        std::string line;
        bool fits{true};

        for (const auto word : std::views::split(a_text, ' ')) {
            const std::string_view wordView{word.begin(), word.end()};
            std::string candidate{line.empty() ? std::string{wordView} : line + " " + std::string{wordView}};

            if (width(candidate) <= maxWidth) {
                line = std::move(candidate);
                continue;
            }

            if (line.empty()) {
                fits = false;
                break;
            }

            lines.push_back(std::move(line));
            line = std::string{wordView};
            fits = width(line) <= maxWidth;

            if (!fits) break;
        }

        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, a_color);

        if (!fits) {
            ImGuiMCP::TextWrapped("%s", a_text.c_str());
            ImGuiMCP::PopStyleColor();
            return;
        }

        lines.push_back(std::move(line));

        for (const auto& text : lines) {
            ImGuiMCP::SetCursorPosX((windowWidth - width(text)) * 0.5f);
            ImGuiMCP::TextUnformatted(text.c_str());
        }

        ImGuiMCP::PopStyleColor();
    }

    constexpr ImGuiMCP::ImVec4 kSuccessText{0.55f, 0.85f, 0.55f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kErrorText{1.0f, 0.45f, 0.4f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kWarningText{0.95f, 0.7f, 0.35f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kHintText{0.7f, 0.7f, 0.75f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kDimText{0.55f, 0.55f, 0.6f, 1.0f};

    void TextColored(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, a_color);
        ImGuiMCP::TextWrapped("%s", a_text.c_str());
        ImGuiMCP::PopStyleColor();
    }

    std::string Translate(const char* a_key, const std::string_view a_value) {
        auto text{UI::Translations::Get(a_key)};

        if (const auto at{text.find("{}")}; at != std::string::npos) {
            text.replace(at, 2, a_value);
        }

        return text;
    }

    struct ConfigResult {
        RE::FormID actor{0};
        std::string message;
        bool failed{false};
        bool saved{false};
    };
    ConfigResult g_configResult;

    // A leveled NPC's record is made as they spawn, so the config can't name it. The player's is Skyrim.esm 00000007
    bool IsNamedByConfig(RE::Actor* a_actor) {
        const auto* const base{a_actor->GetActorBase()};

        return base && !base->IsDynamicForm() && !a_actor->extraList.HasType<RE::ExtraLeveledCreature>() &&
               base->GetFile(0);
    }

    void OnConfigEntryChanged(RE::Actor* a_actor, const std::optional<std::string>& a_error, const char* a_doneKey) {
        g_configResult.actor = a_actor->GetFormID();
        g_configResult.saved = false;

        if (a_error) {
            g_configResult.failed = true;
            g_configResult.message = UI::Translations::Get("oskin_config_entry_failed") + " " + *a_error;
            return;
        }

        const auto reload{UI::ConfigTabs::Reload()};
        const std::string done{a_doneKey ? UI::Translations::Get(a_doneKey) : std::string{}};
        g_configResult.failed = reload.failed;

        // The status line already shows what changed, the reload is only worth telling about when it went wrong
        if (!reload.failed && !reload.filesSkipped) {
            g_configResult.message = done;
            return;
        }

        g_configResult.message = done.empty() ? reload.message : done + " " + reload.message;
    }

    std::string NpcRuleName(RE::Actor* a_actor) {
        const auto* const base{a_actor->GetActorBase()};

        return std::format("{} ({} {:08X})", a_actor->GetDisplayFullName(), base->GetFile(0)->GetFilename(),
                           base->GetLocalFormID());
    }

    void ChangeConfigEntry(RE::Actor* a_actor, const std::optional<std::string>& a_skin) {
        const auto* const base{a_actor->GetActorBase()};
        const std::string plugin{base->GetFile(0)->GetFilename()};
        const auto localFormID{base->GetLocalFormID()};
        const auto formID{std::format("{:08X}", localFormID)};

        // What the config finds with that name, it has to be the NPC
        if (RE::TESDataHandler::GetSingleton()->LookupForm(localFormID, plugin) != base) {
            logger::error("{} {} doesn't find {} ({:08X})", plugin, formID, base->GetName(), base->GetFormID());
            OnConfigEntryChanged(a_actor, std::format("{} {} doesn't find this NPC", plugin, formID), nullptr);
            return;
        }

        if (a_skin) {
            OnConfigEntryChanged(a_actor, Parser::SaveNpcRule(NpcRuleName(a_actor), plugin, formID, *a_skin), nullptr);
            g_configResult.saved = !g_configResult.failed;
        } else {
            OnConfigEntryChanged(a_actor, Parser::RemoveRule(NpcRuleName(a_actor)), "oskin_config_entry_removed");
        }
    }

    const std::vector<std::string>* GetNpcRuleSkins(const Parser::Config& a_config, RE::Actor* a_actor) {
        const auto* const rule{a_config.FindRule(NpcRuleName(a_actor))};

        return rule && !rule->gameSkin ? &rule->skins : nullptr;
    }

    float ButtonWidth(const std::string& a_label) {
        return ImGuiMCP::CalcTextSize(a_label.c_str()).x + ImGuiMCP::GetStyle()->FramePadding.x * 2.0f;
    }

    bool ButtonWithTooltip(const char* a_label, const bool a_enabled, const std::string& a_tooltip) {
        ImGuiMCP::BeginDisabled(!a_enabled);
        const bool clicked{ImGuiMCP::Button(a_label)};
        ImGuiMCP::EndDisabled();

        if (!a_tooltip.empty() && ImGuiMCP::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGuiMCP::SetTooltip("%s", a_tooltip.c_str());
        }

        return clicked;
    }

    void RenderConfigEntry(RE::Actor* a_actor, const std::optional<std::string>& a_current) {
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        const bool named{IsNamedByConfig(a_actor)};
        const auto* const entry{named ? GetNpcRuleSkins(*config, a_actor) : nullptr};
        const bool same{entry && a_current && *entry == std::vector{*a_current}};

        const auto saveLabel{UI::Translations::Get("oskin_config_save_button")};
        const auto removeLabel{UI::Translations::Get("oskin_config_remove_button")};

        const bool showSave{!same};
        float width{showSave ? ButtonWidth(saveLabel) : 0.0f};

        if (entry) {
            width += (showSave ? ImGuiMCP::GetStyle()->ItemSpacing.x : 0.0f) + ButtonWidth(removeLabel);
        }

        std::string saveTooltip{
            UI::Translations::Get(a_actor->IsPlayerRef() ? "oskin_config_player" : "oskin_config_save_hint")};

        if (!named) {
            saveTooltip = UI::Translations::Get("oskin_config_leveled");
        } else if (!a_current) {
            saveTooltip = UI::Translations::Get("oskin_config_no_skin");
        }

        ImGuiMCP::SetCursorPosX((ImGuiMCP::GetWindowSize().x - width) * 0.5f);

        if (showSave && ButtonWithTooltip(saveLabel.c_str(), named && a_current, saveTooltip)) {
            ChangeConfigEntry(a_actor, a_current);
        }

        if (entry) {
            if (showSave) {
                ImGuiMCP::SameLine();
            }

            if (ButtonWithTooltip(removeLabel.c_str(), true, UI::Translations::Get("oskin_config_remove_hint"))) {
                ChangeConfigEntry(a_actor, std::nullopt);
            }
        }

        // Taken again, a change above reloads the config
        const auto shownConfig{Parser::JSONParser::GetInstance().GetConfig()};
        const auto* const shownEntry{named ? GetNpcRuleSkins(*shownConfig, a_actor) : nullptr};

        std::vector<std::pair<ImGuiMCP::ImVec4, std::string>> lines;

        const bool justSaved{g_configResult.saved && g_configResult.actor == a_actor->GetFormID()};
        const char* const sameKey{justSaved ? "oskin_config_entry_saved" : "oskin_config_entry_same"};

        if (shownEntry && a_current && *shownEntry == std::vector{*a_current}) {
            lines.emplace_back(kSuccessText, UI::Translations::Get(sameKey));
        } else if (shownEntry) {
            std::string skins;

            for (const auto& skin : *shownEntry) {
                skins += (skins.empty() ? "" : ", ") + skin;
            }

            lines.emplace_back(kWarningText, UI::Translations::Get("oskin_config_entry_differs") + " " + skins);
        }

        if (g_configResult.actor == a_actor->GetFormID() && !g_configResult.message.empty()) {
            lines.emplace_back(g_configResult.failed ? kErrorText : kSuccessText, g_configResult.message);
        }

        for (const auto& [color, text] : lines) {
            VerticalSpace(0.3f);
            CenteredWrappedText(color, text);
        }
    }

    struct SkinMenuEntry {
        UI::Util::NameOrder order;
        bool mix{false};
    };

    struct SkinMenuCache {
        std::vector<SkinMenuEntry> entries;
        bool valid{false};
        // The config the entries were built from, a reload can change the mixes and the skins left out
        std::uint32_t configGeneration{0};
    };

    std::array<SkinMenuCache, 4> g_skinMenuCache;

    const std::vector<SkinMenuEntry>& GetSkinEntries(RE::Actor* a_actor) {
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        const bool female{UI::Util::IsFemale(a_actor)};
        const bool ube{Skin::IsUBE(a_actor)};
        auto& cache{g_skinMenuCache[(female ? 2 : 0) + (ube ? 1 : 0)]};

        if (!cache.valid || cache.configGeneration != config->generation) {
            cache.entries.clear();

            for (const auto* const skin : config->GetMenuSkins(female, ube)) {
                auto order{UI::Util::MakeNameOrder(skin->name)};
                const bool mix{skin->name != skin->base->name};

                if (mix) {
                    order.displayName += " " + UI::Translations::Get("oskin_skin_is_mix");
                }

                cache.entries.push_back({std::move(order), mix});
            }

            std::ranges::stable_sort(cache.entries, UI::Util::IsBeforeInNameOrder, &SkinMenuEntry::order);
            cache.valid = true;
            cache.configGeneration = config->generation;
        }

        return cache.entries;
    }

    void RenderHeader(RE::Actor* a_actor, const std::string& a_current, const bool a_byHand,
                      const std::string& a_shown) {
        const std::string currentName{a_current.empty() ? UI::Translations::Get("oskin_game_skin") : a_current};
        std::string skinLine{UI::Translations::Get("oskin_current_skin") + ": " + currentName};

        if (a_byHand) {
            skinLine += " " + UI::Translations::Get("oskin_picked_by_hand");
        }

        CenteredText(a_actor->GetDisplayFullName(), 1.4f);

        if (API::OBody::IsAvailable()) {
            const auto label{UI::Translations::Get("oskin_open_in_obody")};
            const float width{ImGuiMCP::CalcTextSize(label.c_str()).x + ImGuiMCP::GetStyle()->FramePadding.x * 2.0f};

            ImGuiMCP::SameLine(ImGuiMCP::GetWindowContentRegionMax().x - width);

            if (ImGuiMCP::SmallButton(label.c_str())) {
                UI::Menu::OpenInOBody(a_actor);
            }
        }

        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4{0.7f, 0.7f, 0.75f, 1.0f});
        CenteredText(skinLine.c_str(), 1.0f);
        ImGuiMCP::PopStyleColor();

        // A skin picked waits for its textures too, then it's the current one
        const bool loading{UI::PreviewPanel::IsLoading(a_actor)};

        if (!a_shown.empty() || loading) {
            const auto shownName{a_shown.empty()            ? currentName
                                 : a_shown.starts_with('<') ? UI::Translations::Get("oskin_mix_draft")
                                                            : a_shown};

            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4{0.96f, 0.84f, 0.58f, 1.0f});

            if (loading) {
                CenteredLoadingText(Translate("oskin_loading_skin", shownName));
            } else {
                CenteredText((UI::Translations::Get("oskin_previewing") + ": " + shownName).c_str(), 1.0f);
            }

            ImGuiMCP::PopStyleColor();
        } else if (UI::Menu::IsPreviewEnabled()) {
            ImGuiMCP::Dummy(ImGuiMCP::ImVec2{0.0f, ImGuiMCP::GetTextLineHeight()});
        }
    }

    void PreviewHovered(RE::Actor* a_actor, const SkinMenuEntry* a_hovered, const SkinMenuEntry* a_focused,
                        const std::optional<std::string>& a_current) {
        if (a_focused && a_focused->order.name != g_lastFocused) {
            g_lastFocused = a_focused->order.name;
            UI::PreviewPanel::Hover(a_actor, a_focused->order.name, a_current);
        }

        // Moving off the list keeps the last skin shown
        if (!a_hovered) {
            g_hoverRest.name.clear();

            return;
        }

        const auto now{std::chrono::steady_clock::now()};
        const auto mouse{ImGuiMCP::GetIO()->MousePos};
        const float movedX{mouse.x - g_hoverRest.anchor.x};
        const float movedY{mouse.y - g_hoverRest.anchor.y};

        if (a_hovered->order.name != g_hoverRest.name ||
            movedX * movedX + movedY * movedY > kHoverStillDistance * kHoverStillDistance) {
            g_hoverRest = {a_hovered->order.name, mouse, now, false};
        } else if (!g_hoverRest.shown && now - g_hoverRest.since >= kHoverRest) {
            g_hoverRest.shown = true;
            UI::PreviewPanel::Hover(a_actor, a_hovered->order.name, a_current);
        }
    }

    bool RenderSkinList(RE::Actor* a_actor, const std::string& a_current, const std::optional<std::string>& a_handPick,
                        const std::string& a_shown) {
        const std::wstring query{UI::Util::ToLowerWide(UI::Util::Utf8ToWide(g_search))};
        const SkinMenuEntry* hovered{nullptr};
        const SkinMenuEntry* focused{nullptr};
        std::optional<std::string> pickedName;
        int index{0};

        for (const auto& entry : GetSkinEntries(a_actor)) {
            if (!query.empty() && entry.order.searchText.find(query) == std::wstring::npos) continue;

            ImGuiMCP::PushID(index);

            const bool previewed{!a_shown.empty() && entry.order.name == a_shown};

            if (previewed) {
                ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, ImGuiMCP::ImVec4{0.96f, 0.84f, 0.58f, 0.35f});
                ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, ImGuiMCP::ImVec4{0.96f, 0.84f, 0.58f, 0.5f});
                ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, ImGuiMCP::ImVec4{0.96f, 0.84f, 0.58f, 0.6f});
            }

            const bool picked{
                ImGuiMCP::Selectable(entry.order.displayName.c_str(), previewed || entry.order.name == a_current)};

            if (previewed) {
                ImGuiMCP::PopStyleColor(3);
            }

            if (picked) {
                pickedName = entry.order.name;
            }

            if (ImGuiMCP::IsItemHovered()) {
                hovered = &entry;
            }

            if (ImGuiMCP::IsItemFocused()) {
                focused = &entry;
            }

            ImGuiMCP::PopID();
            ++index;
        }

        if (pickedName) {
            UI::PreviewPanel::Apply(a_actor, [&] { Skin::Distribution::SetSkinByHand(a_actor, *pickedName); });
            g_lastFocused = *pickedName;

            return true;
        }

        PreviewHovered(a_actor, hovered, focused, a_handPick);

        return false;
    }

    std::string GameFaceNormal(RE::Actor* a_actor) {
        using HeadPartType = RE::BGSHeadPart::HeadPartType;

        auto* const npc{a_actor ? a_actor->GetActorBase() : nullptr};

        if (!npc) return {};

        auto* face{npc->GetCurrentHeadPartByType(HeadPartType::kFace)};
        auto* const race{npc->GetRace()};
        const auto sex{npc->GetSex() == RE::SEX::kFemale ? RE::SEX::kFemale : RE::SEX::kMale};
        const auto* const raceFace{race ? race->faceRelatedData[sex] : nullptr};

        if (!face && raceFace && raceFace->headParts) {
            for (auto* const part : *raceFace->headParts) {
                if (part && part->type == HeadPartType::kFace) {
                    face = part;
                    break;
                }
            }
        }

        const char* const path{
            face && face->textureSet ? face->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal) : nullptr};

        return path ? path : std::string{};
    }

    void RenderSources(RE::Actor* a_actor, const Skin::Distribution::Resolved& a_resolved) {
        using Kind = Skin::Distribution::FieldSource::Kind;

        constexpr std::array<const char*, Skin::kFieldTotal> fieldLabels{
            "oskin_mix_diffuse", "oskin_mix_normal",      "oskin_mix_subsurface", "oskin_mix_specular",
            "oskin_mix_head",    "oskin_mix_head_normal", "oskin_mix_teeth"};

        const auto& resolved{a_resolved};
        // The random skins aren't a rule of the config, so they aren't counted
        const auto used{std::ranges::count_if(
            resolved.rules, [](const Skin::Distribution::RuleUse& a_rule) { return a_rule.used && !a_rule.random; })};
        auto header{UI::Translations::Get("oskin_sources_header")};

        if (used == 1) {
            header += " (" + UI::Translations::Get("oskin_sources_one_rule") + ")";
        } else if (used > 1) {
            header += " (" + Translate("oskin_sources_rules", std::to_string(used)) + ")";
        }

        if (!ImGuiMCP::CollapsingHeader(std::format("{}##OSkinSources", header).c_str())) return;

        constexpr auto flags{ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                             ImGuiMCP::ImGuiTableFlags_BordersOuterH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp};

        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, ImGuiMCP::ImVec2{8.0f, 4.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_TableHeaderBg, UI::Widgets::TableHeaderBackground);

        if (ImGuiMCP::BeginTable("##OSkinSourcesTable", 3, flags)) {
            ImGuiMCP::TableSetupColumn(UI::Translations::Get("oskin_packs_column_part").c_str(), 0, 1.0f);
            ImGuiMCP::TableSetupColumn(UI::Translations::Get("oskin_sources_column_pack").c_str(), 0, 1.2f);
            ImGuiMCP::TableSetupColumn(UI::Translations::Get("oskin_sources_column_from").c_str(), 0, 1.2f);

            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, UI::Widgets::TableHeaderText);
            ImGuiMCP::TableHeadersRow();
            ImGuiMCP::PopStyleColor();

            const auto row = [](const std::string& a_part, const std::string& a_pack, const std::string& a_from,
                                const bool a_dim) {
                ImGuiMCP::TableNextRow();
                ImGuiMCP::TableNextColumn();
                ImGuiMCP::TextUnformatted(a_part.c_str());
                ImGuiMCP::TableNextColumn();

                if (a_dim) {
                    TextColored(kDimText, a_pack);
                } else {
                    ImGuiMCP::TextWrapped("%s", a_pack.c_str());
                }

                ImGuiMCP::TableNextColumn();
                TextColored(kHintText, a_from);
            };

            const auto gameSkin{UI::Translations::Get("oskin_sources_game")};

            const auto source = [&](const std::string& a_part, const Skin::Distribution::FieldSource& a_source) {
                switch (a_source.kind) {
                    case Kind::kHandPick:
                        row(a_part, a_source.pack->name, UI::Translations::Get("oskin_sources_from_hand"), false);
                        break;
                    case Kind::kRule:
                        row(a_part, a_source.pack ? a_source.pack->name : gameSkin,
                            a_source.random ? UI::Translations::Get("oskin_sources_from_random")
                                            : Translate("oskin_sources_from_rule", a_source.rule),
                            !a_source.pack);
                        break;
                    case Kind::kGame:
                        row(a_part, gameSkin, std::string{}, true);
                        break;
                }
            };

            source(UI::Translations::Get("oskin_mix_base"), resolved.baseSource);

            for (std::size_t field = 0; field < Skin::kFieldTotal; ++field) {
                const auto label{UI::Translations::Get(fieldLabels[field])};
                const auto& from{resolved.fields[field]};
                const auto gameNormal{field == Skin::kHeadNormal && from.pack ? GameFaceNormal(a_actor) : ""};

                if (gameNormal.empty()) {
                    source(label, from);
                    continue;
                }

                // A head normal map only fits its own race, so the pack may have none for this NPC's head
                const auto* const pack{Skin::FaceNormalPack(resolved.mix, gameNormal)};

                if (!pack) {
                    row(label, gameSkin, Translate("oskin_sources_none_for_race", from.pack->name), true);
                    continue;
                }

                auto shown{from};
                shown.pack = pack;
                source(label, shown);
            }

            ImGuiMCP::EndTable();
        }

        ImGuiMCP::PopStyleColor();
        ImGuiMCP::PopStyleVar();

        std::string idle;

        for (const auto& rule : resolved.rules) {
            if (!rule.used && !rule.random) {
                idle += (idle.empty() ? "" : ", ") + rule.name;
            }
        }

        if (!idle.empty()) {
            VerticalSpace(0.2f);
            TextColored(kHintText, Translate("oskin_sources_idle", idle));
        }

        VerticalSpace(0.2f);
        TextColored(kHintText, UI::Translations::Get(resolved.byHand ? "oskin_sources_hand" : "oskin_sources_hint"));

        if (!resolved.byHand && a_actor->IsPlayerRef() &&
            !Parser::JSONParser::GetInstance().GetConfig()->DoesPlayerFollowRules()) {
            TextColored(kHintText, UI::Translations::Get("oskin_sources_player"));
        }

        VerticalSpace(0.3f);

        const auto makeLabel{UI::Translations::Get("oskin_make_rule")};

        ImGuiMCP::SetCursorPosX((ImGuiMCP::GetWindowSize().x - ButtonWidth(makeLabel)) * 0.5f);

        if (ButtonWithTooltip(makeLabel.c_str(), true, UI::Translations::Get("oskin_make_rule_hint"))) {
            UI::ConfigEditor::StartRuleFrom(a_actor);
            UI::Menu::ShowConfigEditor();
        }

        VerticalSpace(0.3f);
    }

    enum Part : std::size_t {
        kBase,
        kDiffuse,
        kNormal,
        kSubsurface,
        kSpecular,
        kHead,
        kHeadNormal,
        kTeeth,
        kPartTotal
    };

    constexpr std::array<const char*, kPartTotal> kPartLabels{
        "oskin_mix_base",     "oskin_mix_diffuse", "oskin_mix_normal",      "oskin_mix_subsurface",
        "oskin_mix_specular", "oskin_mix_head",    "oskin_mix_head_normal", "oskin_mix_teeth"};

    struct MixDraft {
        RE::FormID actor{0};
        std::array<std::string, kPartTotal> parts;
        char name[128]{};
        std::string message;
        bool failed{false};
    };

    MixDraft g_mix;

    // Starts from the actor's skin, so a mix can be changed and saved again
    void StartMix(RE::Actor* a_actor, const std::optional<std::string>& a_current) {
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        const auto* const skin{a_current && !a_current->empty() ? config->FindSkin(*a_current) : nullptr};

        g_mix = {};
        g_mix.actor = a_actor->GetFormID();

        if (!skin) return;

        const auto name = [](const Skin::Packs::Pack* a_pack) { return a_pack ? a_pack->name : std::string{}; };

        g_mix.parts = {name(skin->base),     name(skin->diffuse), name(skin->normal),     name(skin->subsurface),
                       name(skin->specular), name(skin->head),    name(skin->headNormal), name(skin->teeth)};

        if (skin->name != skin->base->name) {
            strncpy_s(g_mix.name, skin->name.c_str(), _TRUNCATE);
        }
    }

    Skin::Mix DraftToMix() {
        const auto find = [](const std::string& a_name) {
            return a_name.empty() ? nullptr : Skin::Packs::FindPack(a_name);
        };

        return {.name = g_mix.name,
                .base = find(g_mix.parts[kBase]),
                .diffuse = find(g_mix.parts[kDiffuse]),
                .normal = find(g_mix.parts[kNormal]),
                .subsurface = find(g_mix.parts[kSubsurface]),
                .specular = find(g_mix.parts[kSpecular]),
                .head = find(g_mix.parts[kHead]),
                .headNormal = find(g_mix.parts[kHeadNormal]),
                .teeth = find(g_mix.parts[kTeeth])};
    }

    bool RenderPartCombo(const Part a_part, const bool a_female, const bool a_ube) {
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        const auto none{UI::Translations::Get(a_part == kBase ? "oskin_mix_pick_base" : "oskin_mix_from_base")};
        auto& value{g_mix.parts[a_part]};
        bool changed{false};

        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted(UI::Translations::Get(kPartLabels[a_part]).c_str());
        ImGuiMCP::SameLine(ImGuiMCP::GetWindowSize().x * 0.3f);
        ImGuiMCP::SetNextItemWidth(-FLT_MIN);

        const auto id{std::format("##OSkinMixPart{}", static_cast<std::size_t>(a_part))};

        if (!value.empty()) {
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4{0.96f, 0.84f, 0.58f, 1.0f});
        }

        const bool open{UI::Widgets::BeginCombo(id.c_str(), value.empty() ? none.c_str() : value.c_str())};

        if (!value.empty()) {
            ImGuiMCP::PopStyleColor();
        }

        if (!open) return false;

        if (a_part != kBase && ImGuiMCP::Selectable(none.c_str(), value.empty())) {
            value.clear();
            changed = true;
        }

        for (const auto& pack : Skin::Packs::GetAll()) {
            const auto* const skin{config->FindSkin(pack.name)};

            // The mouth is the same for both sexes and bodies, so a teeth pack may have no female or male files
            if (a_part == kTeeth) {
                const bool mouth{std::ranges::any_of(pack.textures | std::views::keys, [](const std::string& a_key) {
                    return a_key.starts_with("mouth\\");
                })};

                if (!mouth) continue;
            } else if (!skin || !config->CanGiveSkin(*skin, a_female, a_ube)) {
                continue;
            }

            if (ImGuiMCP::Selectable(pack.name.c_str(), stl::cmp(pack.name, value))) {
                value = pack.name;
                changed = true;
            }
        }

        ImGuiMCP::EndCombo();

        return changed;
    }

    void SaveMix(RE::Actor* a_actor) {
        const std::string name{UI::Util::TrimWhitespace(g_mix.name)};

        g_mix.failed = true;

        if (name.empty()) {
            g_mix.message = UI::Translations::Get("oskin_mix_need_name");

            return;
        }

        if (Skin::Packs::FindPack(name)) {
            g_mix.message = UI::Translations::Get("oskin_mix_pack_name");

            return;
        }

        const Parser::MixParts parts{g_mix.parts[kBase],       g_mix.parts[kDiffuse],  g_mix.parts[kNormal],
                                     g_mix.parts[kSubsurface], g_mix.parts[kSpecular], g_mix.parts[kHead],
                                     g_mix.parts[kHeadNormal], g_mix.parts[kTeeth]};

        if (const auto error{Parser::SaveMix(name, parts)}) {
            g_mix.message = UI::Translations::Get("oskin_mix_save_failed") + " " + *error;

            return;
        }

        const auto reload{UI::ConfigTabs::Reload()};

        if (reload.failed) {
            g_mix.message = reload.message;

            return;
        }

        UI::PreviewPanel::Apply(a_actor, [&] { Skin::Distribution::SetSkinByHand(a_actor, name); });
        g_mix.failed = false;
        g_mix.message = UI::Translations::Get("oskin_mix_saved");
    }

    void RenderMixEditor(RE::Actor* a_actor, const std::optional<std::string>& a_current) {
        if (g_mix.actor != a_actor->GetFormID()) {
            StartMix(a_actor, a_current);
        }

        if (!ImGuiMCP::CollapsingHeader(UI::Translations::Get("oskin_mix_header").c_str())) return;

        const bool female{UI::Util::IsFemale(a_actor)};
        bool changed{false};

        ImGuiMCP::Spacing();

        for (std::size_t part = 0; part < kPartTotal; ++part) {
            changed |= RenderPartCombo(static_cast<Part>(part), female, Skin::IsUBE(a_actor));
        }

        if (changed && !g_mix.parts[kBase].empty()) {
            UI::PreviewPanel::Hover(a_actor, Skin::Distribution::SetDraftMix(DraftToMix()), std::nullopt);
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::AlignTextToFramePadding();
        ImGuiMCP::TextUnformatted(UI::Translations::Get("oskin_mix_name").c_str());
        ImGuiMCP::SameLine(ImGuiMCP::GetWindowSize().x * 0.3f);
        ImGuiMCP::SetNextItemWidth(-FLT_MIN);
        ImGuiMCP::InputText("##OSkinMixName", g_mix.name, sizeof(g_mix.name));

        const std::string name{UI::Util::TrimWhitespace(g_mix.name)};
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        const auto* const existing{name.empty() ? nullptr : config->FindSkin(name)};
        const bool replaces{existing && existing->name != existing->base->name};
        const auto saveLabel{UI::Translations::Get(replaces ? "oskin_mix_replace" : "oskin_mix_save")};

        ImGuiMCP::Spacing();
        ImGuiMCP::SetCursorPosX((ImGuiMCP::GetWindowSize().x - ButtonWidth(saveLabel)) * 0.5f);

        if (ButtonWithTooltip(saveLabel.c_str(), !g_mix.parts[kBase].empty(),
                              UI::Translations::Get(replaces ? "oskin_mix_replace_hint" : "oskin_mix_save_hint"))) {
            SaveMix(a_actor);
        }

        // Head color maps from another pack than the body's may not match at the neck
        const auto& head{g_mix.parts[kHead]};
        const auto& bodyColor{g_mix.parts[kDiffuse].empty() ? g_mix.parts[kBase] : g_mix.parts[kDiffuse]};
        const bool mixedNeck{!head.empty() && !stl::cmp(bodyColor, head)};

        if (mixedNeck) {
            VerticalSpace(0.3f);
            CenteredWrappedText(kWarningText, UI::Translations::Get("oskin_mix_neck_hint"));
        }

        if (!g_mix.message.empty()) {
            VerticalSpace(0.3f);
            CenteredWrappedText(g_mix.failed ? kErrorText : kSuccessText, g_mix.message);
        }

        ImGuiMCP::Spacing();
    }
}  // namespace

void UI::SkinsTab::Reset() {
    g_search[0] = '\0';
    g_hoverRest = {};
    g_lastFocused.clear();
    g_configResult = {};
    g_mix = {};

    for (auto& cache : g_skinMenuCache) {
        cache.valid = false;
    }
}

void UI::SkinsTab::OnTargetSelected() {
    g_configResult = {};
    g_mix = {};
}

bool UI::SkinsTab::Render(RE::Actor* a_actor) {
    const auto handPick{Skin::Distribution::GetHandPick(a_actor)};
    const auto resolved{Skin::Distribution::Resolve(a_actor, handPick)};
    const auto shown{UI::PreviewPanel::GetShownSkin(a_actor)};

    const std::string current{handPick ? *handPick : resolved.base ? resolved.baseSource.skin : std::string{}};

    RenderHeader(a_actor, current, handPick.has_value(), shown);

    ImGuiMCP::Spacing();
    ImGuiMCP::Spacing();

    const auto gameSkinLabel{UI::Translations::Get("oskin_game_skin_button")};
    const auto rerollLabel{UI::Translations::Get("oskin_reroll_button")};
    const bool player{a_actor->IsPlayerRef()};
    const auto* const base{a_actor->GetActorBase()};

    // The player only follows the rules that name them, without one a re-roll can only give the game's skin
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    const bool followsRules{player && config->DoesPlayerFollowRules()};
    const bool namedByRule{base && std::ranges::any_of(config->GetRules(), [&](const Parser::Rule& a_rule) {
                               return a_rule.byNpc && a_rule.npcs.contains(base->GetFormID());
                           })};
    const bool canReroll{!player || followsRules || namedByRule};
    const char* const rerollHint{!player || followsRules ? "oskin_reroll_hint"
                                 : canReroll             ? "oskin_reroll_player"
                                                         : "oskin_reroll_player_none"};

    ImGuiMCP::SetCursorPosX((ImGuiMCP::GetWindowSize().x - ButtonWidth(gameSkinLabel) -
                             ImGuiMCP::GetStyle()->ItemSpacing.x * 3.0f - ButtonWidth(rerollLabel)) *
                            0.5f);

    if (ButtonWithTooltip(gameSkinLabel.c_str(), true, UI::Translations::Get("oskin_game_skin_hint"))) {
        UI::PreviewPanel::Apply(a_actor, [&] { Skin::Distribution::SetSkinByHand(a_actor, std::string{}); });

        return true;
    }

    ImGuiMCP::SameLine(0.0f, ImGuiMCP::GetStyle()->ItemSpacing.x * 3.0f);

    if (ButtonWithTooltip(rerollLabel.c_str(), canReroll, UI::Translations::Get(rerollHint))) {
        UI::PreviewPanel::Apply(a_actor, [&] { Skin::Distribution::Reroll(a_actor); });

        return true;
    }

    VerticalSpace(0.4f);
    RenderConfigEntry(a_actor, !current.empty() ? std::optional{current} : std::nullopt);

    VerticalSpace(0.4f);
    RenderSources(a_actor, resolved);

    VerticalSpace(0.4f);
    ImGuiMCP::Separator();
    ImGuiMCP::Spacing();

    UI::PreviewPanel::RenderControls(a_actor);

    ImGuiMCP::Spacing();
    ImGuiMCP::Separator();
    VerticalSpace(0.4f);

    RenderMixEditor(a_actor, !current.empty() ? std::optional{current} : std::nullopt);

    VerticalSpace(0.4f);
    UI::Widgets::SearchField(UI::Translations::Get("oskin_search_skins").c_str(), "##Search", g_search,
                             sizeof(g_search), ImGuiMCP::IsWindowAppearing());

    ImGuiMCP::Spacing();
    ImGuiMCP::Separator();
    ImGuiMCP::Spacing();

    // Only the list scrolls, so the header and the controls stay in view
    const auto hints{UI::PreviewPanel::GetHints(a_actor)};
    float hintHeight{0.0f};

    if (!hints.empty()) {
        hintHeight = UI::Widgets::ControlHintsHeight(hints) + ImGuiMCP::GetStyle()->ItemSpacing.y * 3.0f + 2.0f;
    }

    bool picked{false};

    if (ImGuiMCP::BeginChild("##SkinListScroll", ImGuiMCP::ImVec2{0.0f, -hintHeight}, ImGuiMCP::ImGuiChildFlags_None,
                             ImGuiMCP::ImGuiWindowFlags_NavFlattened)) {
        picked = RenderSkinList(a_actor, current, handPick, shown);
    }

    ImGuiMCP::EndChild();

    if (!hints.empty()) {
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();
        UI::Widgets::ControlHints(hints);
    }

    return picked;
}
