#pragma once

namespace UI::Lookups {
    enum class Kind {
        kPlugin,
        kNpcRecord,  // NPC records, by plugin and FormID
        kFaction,    // factions, by editor ID
        kRace,       // races, by editor ID
        kClass,      // classes, by editor ID
        kSkin,       // the packs and mixes, of both sexes
        kSkinFemale,
        kSkinMale,
        kTotal
    };

    struct Item {
        std::string label;
        std::string value;
        std::string plugin;
        std::wstring search;
        bool female{false};
    };

    const std::vector<Item>& Get(Kind a_kind);

    void RebuildSkins();
    [[nodiscard]] std::uint32_t Generation();

    std::vector<const Item*> FromActor(Kind a_kind, RE::Actor* a_actor);

    const RE::TESForm* FindRecord(std::string_view a_plugin, std::string_view a_formID);
}  // namespace UI::Lookups
