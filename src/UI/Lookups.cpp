#include "UI/Lookups.h"

#include "Config/Config.h"
#include "Skin/Distribution.h"
#include "STL.h"
#include "UI/Util.h"

namespace {
    using UI::Lookups::Item;
    using UI::Lookups::Kind;

    std::array<std::vector<Item>, static_cast<std::size_t>(Kind::kTotal)> g_lists;
    bool g_built{false};
    std::uint32_t g_generation{0};

    std::vector<Item>& List(const Kind a_kind) { return g_lists[static_cast<std::size_t>(a_kind)]; }

    // The game keeps the editor IDs of races, the others come from powerofthree's Tweaks
    std::string EditorID(const RE::TESForm* a_form) {
        switch (a_form->GetFormType()) {
            case RE::FormType::Race:
                return a_form->GetFormEditorID();
            default: {
                const char* const editorID{stl::func ? stl::func(a_form->GetFormID()) : nullptr};

                return editorID ? editorID : "";
            }
        }
    }

    std::string FormIDText(const RE::TESForm* a_form) { return std::format("{:08X}", a_form->GetLocalFormID()); }

    std::string PluginOf(const RE::TESForm* a_form) {
        const auto* const file{a_form->GetFile(0)};

        return file ? std::string{file->GetFilename()} : std::string{};
    }

    void Add(const Kind a_kind, std::string a_label, std::string a_value, std::string a_plugin = {},
             const bool a_female = false) {
        auto search{UI::Util::ToLowerWide(UI::Util::Utf8ToWide(a_label))};
        List(a_kind).push_back(
            {std::move(a_label), std::move(a_value), std::move(a_plugin), std::move(search), a_female});
    }

    std::string EditorIDLabel(const std::string& a_editorID, const char* a_name) {
        const std::string_view name{a_name ? a_name : ""};

        return name.empty() || name == a_editorID ? a_editorID : std::format("{} ({})", a_editorID, name);
    }

    void SortByLabel(std::vector<Item>& a_items) {
        std::ranges::stable_sort(
            a_items, [](const Item& a, const Item& b) { return UI::Util::CompareLinguistic(a.search, b.search) < 0; });
    }

    void BuildPlugins(RE::TESDataHandler* a_dataHandler) {
        std::vector<const RE::TESFile*> files;

        for (const auto* const file : a_dataHandler->files) {
            if (file && file->compileIndex != 0xFF) {
                files.push_back(file);
            }
        }

        // In the load order, light plugins by their own index
        std::ranges::sort(files, {}, [](const RE::TESFile* a_file) {
            return (static_cast<std::uint32_t>(a_file->compileIndex) << 16) | a_file->smallFileCompileIndex;
        });

        for (const auto* const file : files) {
            Add(Kind::kPlugin, std::string{file->GetFilename()}, std::string{file->GetFilename()});
        }
    }

    bool IsAdultPersonRace(const RE::TESRace* a_race) {
        return a_race->HasKeywordString("ActorTypeNPC") && !a_race->IsChildRace();
    }

    std::size_t g_skippedNpcs{0};
    std::size_t g_skippedRaces{0};

    void BuildNpcs(RE::TESDataHandler* a_dataHandler) {
        for (const auto* const npc : a_dataHandler->GetFormArray<RE::TESNPC>()) {
            if (!npc || npc->IsPlayer() || npc->IsDynamicForm()) continue;

            // Children and creatures never get a skin from OSkin
            // An NPC without a race is kept, as the race may come from a template
            if (npc->race && !IsAdultPersonRace(npc->race)) {
                ++g_skippedNpcs;
                continue;
            }

            const std::string name{npc->GetName() ? npc->GetName() : ""};
            const auto plugin{PluginOf(npc)};

            if (plugin.empty()) continue;

            const auto formID{FormIDText(npc)};
            const auto shownName{name.empty() ? EditorID(npc) : name};
            const bool female{npc->GetSex() == RE::SEX::kFemale};

            Add(Kind::kNpcRecord, std::format("{} ({}, {})", shownName.empty() ? "?" : shownName, plugin, formID),
                formID, plugin, female);
        }

        SortByLabel(List(Kind::kNpcRecord));
    }

    template <typename T>
    void BuildByEditorID(RE::TESDataHandler* a_dataHandler, const Kind a_kind) {
        for (const auto* const form : a_dataHandler->GetFormArray<T>()) {
            if (!form) continue;

            if constexpr (std::is_same_v<T, RE::TESRace>) {
                if (!IsAdultPersonRace(form)) {
                    ++g_skippedRaces;
                    continue;
                }
            }

            const auto editorID{EditorID(form)};

            // The config names them by editor ID, one without it can't be named
            if (editorID.empty()) continue;

            Add(a_kind, EditorIDLabel(editorID, form->GetName()), editorID);
        }

        SortByLabel(List(a_kind));
    }

    void BuildSkins() {
        const auto config{Parser::JSONParser::GetInstance().GetConfig()};
        std::vector<std::pair<UI::Util::NameOrder, const Skin::Mix*>> skins;

        for (const auto* const skin : config->GetAllSkins()) {
            skins.emplace_back(UI::Util::MakeNameOrder(skin->name), skin);
        }

        std::ranges::stable_sort(skins, UI::Util::IsBeforeInNameOrder,
                                 &std::pair<UI::Util::NameOrder, const Skin::Mix*>::first);

        for (const auto& [order, skin] : skins) {
            Add(Kind::kSkin, order.displayName, order.name);

            if (skin->base->female) {
                Add(Kind::kSkinFemale, order.displayName, order.name, {}, true);
            }

            if (skin->base->male) {
                Add(Kind::kSkinMale, order.displayName, order.name);
            }
        }
    }

    void Build() {
        auto* const dataHandler{RE::TESDataHandler::GetSingleton()};

        g_built = true;

        BuildPlugins(dataHandler);
        BuildNpcs(dataHandler);
        BuildByEditorID<RE::TESFaction>(dataHandler, Kind::kFaction);
        BuildByEditorID<RE::TESRace>(dataHandler, Kind::kRace);
        BuildByEditorID<RE::TESClass>(dataHandler, Kind::kClass);
        BuildSkins();

        logger::info("Config Editor lists: {} plugins, {} NPC records, {} factions, {} races, {} classes, {} skins",
                     List(Kind::kPlugin).size(), List(Kind::kNpcRecord).size(), List(Kind::kFaction).size(),
                     List(Kind::kRace).size(), List(Kind::kClass).size(), List(Kind::kSkin).size());
        logger::info("Config Editor lists left out {} NPC records and {} races of children and creatures",
                     g_skippedNpcs, g_skippedRaces);

        if (!stl::func) {
            logger::warn("powerofthree's Tweaks isn't there, so the factions and classes can't be listed");
        }
    }

    std::vector<std::pair<std::string, std::string>> ActorValues(const Kind a_kind, RE::Actor* a_actor) {
        std::vector<std::pair<std::string, std::string>> values;
        const auto* const base{a_actor->GetActorBase()};

        if (!base) return values;

        switch (a_kind) {
            case Kind::kPlugin:
                values.emplace_back("", PluginOf(base));
                values.emplace_back("", PluginOf(a_actor));
                break;
            case Kind::kNpcRecord:
                // A leveled NPC's record is made as they spawn, the config can't name it
                if (!base->IsDynamicForm()) {
                    values.emplace_back(PluginOf(base), FormIDText(base));
                }
                break;
            case Kind::kFaction:
                for (const auto& rank : base->factions) {
                    if (rank.faction) {
                        values.emplace_back("", EditorID(rank.faction));
                    }
                }
                break;
            case Kind::kRace:
                if (const auto* const race{a_actor->GetRace()}) {
                    values.emplace_back("", EditorID(race));
                }
                break;
            case Kind::kClass:
                if (base->npcClass) {
                    values.emplace_back("", EditorID(base->npcClass));
                }
                break;
            case Kind::kSkin:
            case Kind::kSkinFemale:
            case Kind::kSkinMale:
                if (const auto skin{Skin::Distribution::GetShownSkinName(a_actor)}; !skin.empty()) {
                    values.emplace_back("", skin);
                }
                break;
            case Kind::kTotal:
                break;
        }

        return values;
    }
}  // namespace

const std::vector<Item>& UI::Lookups::Get(const Kind a_kind) {
    if (!g_built) {
        Build();
    }

    return List(a_kind);
}

void UI::Lookups::RebuildSkins() {
    if (!g_built) return;

    for (const auto kind : {Kind::kSkin, Kind::kSkinFemale, Kind::kSkinMale}) {
        List(kind).clear();
    }

    BuildSkins();
    ++g_generation;
}

std::uint32_t UI::Lookups::Generation() { return g_generation; }

const RE::TESForm* UI::Lookups::FindRecord(const std::string_view a_plugin, const std::string_view a_formID) {
    auto* const dataHandler{RE::TESDataHandler::GetSingleton()};
    const auto* const file{dataHandler ? dataHandler->LookupModByName(a_plugin) : nullptr};

    if (!file) return nullptr;

    RE::FormID formID{};
    const auto* const end{a_formID.data() + a_formID.size()};

    if (const auto [last, error]{std::from_chars(a_formID.data(), end, formID, 16)};
        error != std::errc{} || last != end) {
        return nullptr;
    }

    return dataHandler->LookupForm(formID & (file->IsLight() ? 0xFFFu : 0xFFFFFFu), a_plugin);
}

std::vector<const Item*> UI::Lookups::FromActor(const Kind a_kind, RE::Actor* a_actor) {
    std::vector<const Item*> items;

    if (!a_actor) return items;

    const auto values{ActorValues(a_kind, a_actor)};

    if (values.empty()) return items;

    for (const auto& item : Get(a_kind)) {
        const bool has{std::ranges::any_of(
            values, [&](const auto& a_value) { return a_value.first == item.plugin && a_value.second == item.value; })};

        if (has && std::ranges::find(items, &item) == items.end()) {
            items.push_back(&item);
        }
    }

    return items;
}
