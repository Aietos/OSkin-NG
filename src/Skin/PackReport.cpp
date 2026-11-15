#include "Skin/PackReport.h"

#include "STL.h"

namespace {
    using namespace Skin::PackReport;

    std::mutex g_reportsLock;
    std::unordered_map<const Skin::Packs::Pack*, Report> g_reports;

    std::optional<Channel> ChannelOf(const std::string_view a_stem) {
        const auto underscore{a_stem.rfind('_')};

        if (underscore == std::string_view::npos) return kDiffuse;

        const auto suffix{a_stem.substr(underscore + 1)};

        if (suffix == "msn" || suffix == "n") return kNormal;

        if (suffix == "sk") return kSubsurface;

        if (suffix == "s") return kSpecular;

        if (suffix == "d") return kDiffuse;

        // femalebody_1 is a color map, femalebody_1_e or _rmaos are other maps
        if (std::ranges::all_of(suffix, [](const char c) { return c >= '0' && c <= '9'; })) return kDiffuse;

        return std::nullopt;
    }

    std::optional<Part> PartOf(const std::string_view a_folder, const std::string_view a_stem) {
        if (a_folder == "mouth") return kTeeth;

        if (a_folder == "sos" || a_stem.contains("genital") || a_stem.contains("schlong") ||
            a_stem.contains("vagina")) {
            return kGenitals;
        }

        if (a_stem.contains("head")) return a_stem.contains("vampire") ? kVampireHead : kHead;

        if (a_stem.contains("hands")) return kHands;

        if (a_stem.contains("body")) return kBody;

        return std::nullopt;
    }

    std::optional<bool> IsFemale(const std::string_view a_folder, const std::string_view a_stem) {
        if (a_folder.ends_with("female") || a_folder.starts_with("female")) return true;

        if (a_folder.ends_with("male") || a_folder.starts_with("male") || a_folder == "sos") return false;

        if (a_stem.contains("female")) return true;

        if (a_stem.contains("male")) return false;

        return std::nullopt;
    }

    bool IsBeastFolder(const std::string_view a_folder) {
        return a_folder.contains("khajiit") || a_folder.contains("argonian");
    }

    void AddRace(std::vector<std::string>& a_races, const std::string_view a_folder) {
        if (std::ranges::find(a_races, a_folder) == a_races.end()) a_races.emplace_back(a_folder);
    }

    void AddFile(Side& a_side, const Part a_part, const Channel a_channel, const std::string_view a_folder) {
        auto& cell{a_side.cells[a_part][a_channel]};

        a_side.any = true;
        a_side.standard = true;

        // Head normal maps never fall back to another folder, each race's head is its own mesh
        const bool headNormal{(a_part == kHead || a_part == kVampireHead) && a_channel == kNormal};

        if (headNormal || Skin::Packs::IsSharedRaceFolder(a_folder)) {
            AddRace(cell.races, a_folder);
        } else {
            cell.allRaces = true;
        }
    }

    Report MakeReport(const Skin::Packs::Pack& a_pack) {
        Report report;

        for (const auto& key : a_pack.textures | std::views::keys) {
            const std::string_view path{key};
            const auto slash{path.find('\\')};
            const auto folder{slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash)};
            auto stem{path.substr(path.rfind('\\') + 1)};

            if (stem.ends_with(".dds")) stem.remove_suffix(4);

            if (const auto detail{stem.find("headdetail_")}; detail != std::string_view::npos) {
                if (const auto female{IsFemale(folder, stem)}) {
                    auto& side{*female ? report.female : report.male};
                    const std::string name{stem.substr(detail + 11)};

                    side.any = true;

                    if (std::ranges::find(side.faceDetails, name) == side.faceDetails.end()) {
                        side.faceDetails.push_back(name);
                    }

                    continue;
                }
            }

            const auto part{PartOf(folder, stem)};
            const auto channel{ChannelOf(stem)};

            if (!part || !channel) {
                ++report.otherFiles;
                continue;
            }

            if (*part == kBody || *part == kHands || *part == kHead) {
                std::string group;

                if (folder.ends_with("old")) {
                    group = "elders";
                } else if (stem.contains("afflicted")) {
                    group = "afflicted";
                } else if (stem.contains("briarheart")) {
                    // The Forsworn Briarhearts' bare chest, in textures\armor\briarheart
                    group = "briarheart";
                } else if (!stem.starts_with("female") && !stem.starts_with("male")) {
                    const auto name{
                        stem.substr(0, (std::min)({stem.find("body"), stem.find("hands"), stem.find("head")}))};

                    group = std::format("npc:{}", name);
                }

                if (const auto female{group.empty() ? std::nullopt : IsFemale(folder, stem)}) {
                    auto& side{*female ? report.female : report.male};

                    side.any = true;
                    side.specials[group][*part] = true;
                    continue;
                }
            }

            // The mouth is the same for both sexes
            if (*part == kTeeth) {
                AddFile(report.female, kTeeth, *channel, folder);
                AddFile(report.male, kTeeth, *channel, folder);
                continue;
            }

            const auto female{IsFemale(folder, stem)};

            if (!female) {
                ++report.otherFiles;
                continue;
            }

            auto& side{*female ? report.female : report.male};

            if (IsBeastFolder(folder)) {
                side.any = true;
                AddRace(side.beastRaces, folder);
                continue;
            }

            AddFile(side, *part, *channel, folder);
        }

        for (auto* const side : {&report.female, &report.male}) {
            std::ranges::sort(side->beastRaces);
            std::ranges::sort(side->faceDetails);

            for (auto& channels : side->cells) {
                for (auto& cell : channels) {
                    std::ranges::sort(cell.races);
                }
            }
        }

        return report;
    }
}  // namespace

const Report& Skin::PackReport::Get(const Packs::Pack& a_pack) {
    std::lock_guard lock{g_reportsLock};

    const auto report{g_reports.find(&a_pack)};

    if (report != g_reports.end()) return report->second;

    return g_reports.emplace(&a_pack, MakeReport(a_pack)).first->second;
}

std::string Skin::PackReport::RaceName(const std::string_view a_folder) {
    constexpr std::array<std::pair<std::string_view, const char*>, 10> races{{
        {"breton", "BretonRace"},
        {"darkelf", "DarkElfRace"},
        {"highelf", "HighElfRace"},
        {"imperial", "ImperialRace"},
        {"nord", "NordRace"},
        {"redguard", "RedguardRace"},
        {"woodelf", "WoodElfRace"},
        {"orc", "OrcRace"},
        {"khajiit", "KhajiitRace"},
        {"argonian", "ArgonianRace"},
    }};

    // UBE's head folder is for every UBE head
    if (a_folder.starts_with("!ube")) return "UBE";

    // The female and male folders hold the Nord head
    const char* editorID{a_folder == "female" || a_folder == "male" ? "NordRace" : nullptr};

    for (const auto& [prefix, id] : races) {
        if (!editorID && a_folder.contains(prefix)) editorID = id;
    }

    const auto* const race{editorID ? RE::TESForm::LookupByEditorID<RE::TESRace>(editorID) : nullptr};
    const char* const name{race ? race->GetName() : nullptr};

    return name && *name ? std::string{name} : std::string{a_folder};
}
