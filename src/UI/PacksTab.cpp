#include "UI/PacksTab.h"

#include "Config/Config.h"
#include "STL.h"
#include "SKSEMenuFramework.h"
#include "Skin/PackReport.h"
#include "UI/Translations.h"
#include "UI/Util.h"
#include "UI/Widgets.h"

namespace {
    using namespace Skin::PackReport;

    constexpr ImGuiMCP::ImVec4 kWarningText{0.95f, 0.7f, 0.35f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kHintText{0.7f, 0.7f, 0.75f, 1.0f};
    constexpr ImGuiMCP::ImVec4 kDimText{0.55f, 0.55f, 0.6f, 1.0f};

    constexpr std::array<const char*, kPartTotal> kPartLabels{"oskin_packs_part_body",     "oskin_packs_part_hands",
                                                              "oskin_packs_part_head",     "oskin_packs_part_vampire",
                                                              "oskin_packs_part_genitals", "oskin_packs_part_teeth"};

    constexpr std::array<const char*, kChannelTotal> kChannelColumns{"oskin_mix_diffuse", "oskin_mix_normal",
                                                                     "oskin_mix_subsurface", "oskin_mix_specular"};

    constexpr std::array<const char*, kChannelTotal> kChannelWords{
        "oskin_packs_channel_diffuse", "oskin_packs_channel_normal", "oskin_packs_channel_subsurface",
        "oskin_packs_channel_specular"};

    std::string Translate(const char* a_key, std::initializer_list<std::string_view> a_values = {}) {
        auto text{UI::Translations::Get(a_key)};

        for (const auto value : a_values) {
            if (const auto at{text.find("{}")}; at != std::string::npos) {
                text.replace(at, 2, value);
            }
        }

        return text;
    }

    std::string Join(const std::vector<std::string>& a_items) {
        std::string joined;

        for (const auto& item : a_items) {
            joined += (joined.empty() ? "" : ", ") + item;
        }

        return joined;
    }

    void TextColored(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, a_color);
        ImGuiMCP::TextWrapped("%s", a_text.c_str());
        ImGuiMCP::PopStyleColor();
    }

    void VerticalSpace(const float a_lines) {
        ImGuiMCP::Dummy(ImGuiMCP::ImVec2{0.0f, ImGuiMCP::GetTextLineHeight() * a_lines});
    }

    std::vector<std::string> RaceNames(const std::vector<std::string>& a_folders) {
        std::vector<std::string> names;

        for (const auto& folder : a_folders) {
            names.push_back(RaceName(folder));
        }

        return names;
    }

    bool HasTeeth(const Report& a_report) { return std::ranges::any_of(a_report.female.cells[kTeeth], &Cell::Has); }

    std::vector<std::string> Warnings(const Skin::Packs::Pack& a_pack, const Report& a_report,
                                      const Parser::Config& a_config) {
        std::vector<std::string> warnings;

        // A teeth pack is fine without them, a mix takes its mouth
        if (!a_pack.female && !a_pack.male && !HasTeeth(a_report)) {
            warnings.push_back(Translate("oskin_packs_no_sex"));
        }

        const auto* const skin{a_config.FindSkin(a_pack.name)};
        // UBE hands use the body's textures
        const bool ube{skin && Parser::Config::IsUBESkin(*skin)};

        for (const bool female : {true, false}) {
            const auto& side{female ? a_report.female : a_report.male};

            if (!(female ? a_pack.female : a_pack.male)) continue;

            std::vector<std::string> wristChannels;

            for (std::size_t channel = 0; channel < kChannelTotal; ++channel) {
                if (side.cells[kBody][channel].Has() && !side.cells[kHands][channel].Has()) {
                    wristChannels.push_back(Translate(kChannelWords[channel]));
                }
            }

            if (!ube && !wristChannels.empty()) {
                warnings.push_back(
                    Translate(female ? "oskin_packs_wrists_female" : "oskin_packs_wrists_male", {Join(wristChannels)}));
            }

            if (side.cells[kBody][kDiffuse].Has() != side.cells[kHead][kDiffuse].Has()) {
                warnings.push_back(Translate(female ? "oskin_packs_neck_female" : "oskin_packs_neck_male"));
            }
        }

        if (a_pack.duplicateFiles > 0) {
            warnings.push_back(Translate("oskin_packs_duplicates", {std::to_string(a_pack.duplicateFiles)}));
        }

        return warnings;
    }

    void RenderCell(const Cell& a_cell) {
        if (a_cell.allRaces) {
            ImGuiMCP::TextWrapped("%s", Translate("oskin_packs_all_races").c_str());
        } else if (!a_cell.races.empty()) {
            ImGuiMCP::TextWrapped("%s", Join(RaceNames(a_cell.races)).c_str());
        } else {
            TextColored(kHintText, "-");
        }
    }

    void RenderTable(const char* a_id, const Side& a_side) {
        constexpr auto flags{ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                             ImGuiMCP::ImGuiTableFlags_BordersOuterH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp};

        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, ImGuiMCP::ImVec2{8.0f, 4.0f});
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_TableHeaderBg, UI::Widgets::TableHeaderBackground);

        if (!ImGuiMCP::BeginTable(a_id, 1 + kChannelTotal, flags)) {
            ImGuiMCP::PopStyleColor();
            ImGuiMCP::PopStyleVar();

            return;
        }

        ImGuiMCP::TableSetupColumn(Translate("oskin_packs_column_part").c_str(), 0, 1.2f);

        for (const auto* const column : kChannelColumns) {
            ImGuiMCP::TableSetupColumn(Translate(column).c_str(), 0, 1.0f);
        }

        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, UI::Widgets::TableHeaderText);
        ImGuiMCP::TableHeadersRow();
        ImGuiMCP::PopStyleColor();

        for (std::size_t part = 0; part < kPartTotal; ++part) {
            if (std::ranges::none_of(a_side.cells[part], &Cell::Has)) continue;

            ImGuiMCP::TableNextRow();
            ImGuiMCP::TableNextColumn();
            ImGuiMCP::TextUnformatted(Translate(kPartLabels[part]).c_str());

            for (const auto& cell : a_side.cells[part]) {
                ImGuiMCP::TableNextColumn();
                RenderCell(cell);
            }
        }

        ImGuiMCP::EndTable();
        ImGuiMCP::PopStyleColor();
        ImGuiMCP::PopStyleVar();
    }

    char g_search[128]{};

    std::string BodyTag(const Skin::Packs::Pack& a_pack, const Parser::Config& a_config) {
        const auto* const skin{a_config.FindSkin(a_pack.name)};

        return skin && Parser::Config::IsUBESkin(*skin) ? std::string{"UBE"} : std::string{};
    }

    std::string Details(const Skin::Packs::Pack& a_pack, const std::string& a_bodyTag) {
        const char* const sexKey{a_pack.female && a_pack.male ? "oskin_packs_sex_both"
                                 : a_pack.female              ? "oskin_packs_sex_female"
                                 : a_pack.male                ? "oskin_packs_sex_male"
                                                              : "oskin_packs_sex_none"};
        const auto count{a_pack.textures.size()};
        std::string details{Translate(sexKey) + ", " +
                            (count == 1 ? Translate("oskin_packs_texture_one")
                                        : Translate("oskin_packs_textures", {std::to_string(count)}))};

        if (!a_bodyTag.empty()) {
            details += ", " + a_bodyTag;
        }

        return details;
    }

    bool PackHeader(const Skin::Packs::Pack& a_pack, const std::string& a_details, const std::size_t a_warnings,
                    const int a_foldAll) {
        if (a_foldAll != 0) {
            ImGuiMCP::SetNextItemOpen(a_foldAll > 0);
        }

        const bool open{ImGuiMCP::CollapsingHeader(std::format("##OSkinPack{}", a_pack.name).c_str())};
        const auto min{ImGuiMCP::GetItemRectMin()};
        const auto max{ImGuiMCP::GetItemRectMax()};
        const auto& padding{ImGuiMCP::GetStyle()->FramePadding};
        auto* const drawList{ImGuiMCP::GetWindowDrawList()};
        const float y{min.y + padding.y};
        float x{min.x + ImGuiMCP::GetFontSize() + padding.x * 3.0f};

        ImGuiMCP::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2{x, y},
                                             ImGuiMCP::GetColorU32(ImGuiMCP::ImGuiCol_Text), a_pack.name.c_str());
        x += ImGuiMCP::CalcTextSize(a_pack.name.c_str()).x + ImGuiMCP::GetFontSize() * 0.8f;
        ImGuiMCP::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2{x, y}, ImGuiMCP::GetColorU32(kDimText),
                                             a_details.c_str());

        if (a_warnings > 0) {
            const auto warning{a_warnings == 1 ? Translate("oskin_packs_warning_one")
                                               : Translate("oskin_packs_warnings", {std::to_string(a_warnings)})};
            const float width{ImGuiMCP::CalcTextSize(warning.c_str()).x};

            ImGuiMCP::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2{max.x - padding.x * 2.0f - width, y},
                                                 ImGuiMCP::GetColorU32(kWarningText), warning.c_str());
        }

        return open;
    }

    void Note(const ImGuiMCP::ImVec4& a_color, const std::string& a_text) {
        TextColored(a_color, a_text);
        VerticalSpace(0.25f);
    }

    void SideHeader(const std::string& a_text) {
        VerticalSpace(0.5f);
        TextColored(UI::Widgets::TableHeaderText, a_text);
        VerticalSpace(0.15f);
    }

    void RenderPack(const Skin::Packs::Pack& a_pack, const Report& a_report, const std::vector<std::string>& a_warnings,
                    const std::string& a_bodyTag, const int a_foldAll) {
        if (!PackHeader(a_pack, Details(a_pack, a_bodyTag), a_warnings.size(), a_foldAll)) return;

        const float indent{ImGuiMCP::GetFontSize() * 0.8f};

        ImGuiMCP::Indent(indent);

        for (const bool female : {true, false}) {
            const auto& side{female ? a_report.female : a_report.male};

            if (!side.any) continue;

            SideHeader(Translate(female ? "oskin_packs_women" : "oskin_packs_men"));

            if (side.standard) {
                RenderTable(female ? "##OSkinPackFemale" : "##OSkinPackMale", side);
                VerticalSpace(0.3f);
            }

            for (const auto& [group, parts] : side.specials) {
                std::vector<std::string> names;

                for (const auto part : {kBody, kHands, kHead}) {
                    if (parts[part]) {
                        names.push_back(Translate(kPartLabels[part]));
                    }
                }

                if (group.starts_with("npc:")) {
                    auto npc{group.substr(4)};

                    if (!npc.empty()) {
                        npc[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(npc[0])));
                    }

                    Note(kHintText, Translate("oskin_packs_special_npc", {npc, Join(names)}));
                } else {
                    const char* const key{group == "elders"       ? "oskin_packs_special_elders"
                                          : group == "briarheart" ? "oskin_packs_special_briarheart"
                                                                  : "oskin_packs_special_afflicted"};

                    Note(kHintText, Translate(key, {Join(names)}));
                }
            }

            if (!side.faceDetails.empty()) {
                Note(kHintText, Translate("oskin_packs_special_details", {Join(side.faceDetails)}));
            }

            // A pack of normal maps or teeth is for mixes and rules of parts, random skins leave it out
            if ((female ? a_pack.female : a_pack.male) && !side.cells[kBody][kDiffuse].Has()) {
                Note(kHintText, Translate(female ? "oskin_packs_not_random_female" : "oskin_packs_not_random_male"));
            }

            if (!side.beastRaces.empty()) {
                Note(kHintText, Translate("oskin_packs_beast", {Join(RaceNames(side.beastRaces))}));
            }
        }

        VerticalSpace(0.2f);

        if (a_bodyTag == "UBE") {
            Note(kHintText, Translate("oskin_packs_ube_only"));
            Note(kHintText, Translate("oskin_packs_ube_hands"));
        }

        if (!a_pack.female && !a_pack.male && HasTeeth(a_report)) {
            Note(kHintText, Translate("oskin_packs_teeth_only"));
        }

        if (a_report.otherFiles > 0) {
            Note(kHintText, Translate("oskin_packs_other", {std::to_string(a_report.otherFiles)}));
        }

        for (const auto& warning : a_warnings) {
            Note(kWarningText, warning);
        }

        ImGuiMCP::Unindent(indent);
        VerticalSpace(0.6f);
    }
    int RenderToolbar(const std::size_t a_shown, const std::size_t a_total) {
        UI::Widgets::SearchField(Translate("oskin_packs_search").c_str(), "##OSkinPackSearch", g_search,
                                 sizeof(g_search));

        // Always there, so the list doesn't move when a search starts
        TextColored(kDimText, Translate("oskin_packs_shown", {std::to_string(a_shown), std::to_string(a_total)}));

        int foldAll{0};

        if (a_shown > 0) {
            const auto expandLabel{Translate("oskin_expand_all")};
            const auto collapseLabel{Translate("oskin_collapse_all")};
            const auto& padding{ImGuiMCP::GetStyle()->FramePadding};
            const float width{ImGuiMCP::CalcTextSize(expandLabel.c_str()).x +
                              ImGuiMCP::CalcTextSize(collapseLabel.c_str()).x + padding.x * 4.0f +
                              ImGuiMCP::GetStyle()->ItemSpacing.x};

            ImGuiMCP::SameLine(ImGuiMCP::GetWindowContentRegionMax().x - width);

            if (ImGuiMCP::SmallButton(expandLabel.c_str())) {
                foldAll = 1;
            }

            ImGuiMCP::SameLine();

            if (ImGuiMCP::SmallButton(collapseLabel.c_str())) {
                foldAll = -1;
            }
        }

        VerticalSpace(0.4f);

        return foldAll;
    }
}  // namespace

void UI::PacksTab::Render() {
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    const auto& packs{Skin::Packs::GetAll()};

    VerticalSpace(0.3f);
    TextColored(kHintText, Translate("oskin_packs_intro"));

    if (packs.empty()) {
        VerticalSpace(0.6f);
        TextColored(kHintText, Translate("oskin_packs_none"));

        return;
    }

    const auto query{UI::Util::ToLowerWide(UI::Util::Utf8ToWide(g_search))};
    std::vector<const Skin::Packs::Pack*> shown;

    for (const auto& pack : packs) {
        const auto text{UI::Util::ToLowerWide(UI::Util::Utf8ToWide(pack.name + " " + BodyTag(pack, *config)))};

        if (query.empty() || text.find(query) != std::wstring::npos) {
            shown.push_back(&pack);
        }
    }

    const int foldAll{RenderToolbar(shown.size(), packs.size())};

    if (shown.empty()) {
        TextColored(kHintText, Translate("oskin_packs_no_match"));

        return;
    }

    for (const auto* const pack : shown) {
        const auto& report{Get(*pack)};

        RenderPack(*pack, report, Warnings(*pack, report, *config), BodyTag(*pack, *config), foldAll);
    }
}

void UI::PacksTab::Reset() { g_search[0] = '\0'; }
