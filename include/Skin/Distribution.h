#pragma once

#include "Skin/Apply.h"

namespace Parser {
    struct Rule;
}  // namespace Parser

namespace Skin::Distribution {
    constexpr std::uint32_t CosaveUID{'OSKN'};

    void ApplyForActor(RE::Actor* a_actor, RE::NiAVObject* a_root, std::string_view a_source);

    void ShowSkin(RE::Actor* a_actor, RE::NiAVObject* a_root, const std::optional<std::string>& a_skin,
                  std::string_view a_source);

    std::string SetDraftMix(const Mix& a_mix);

    std::optional<std::string> GetHandPick(RE::Actor* a_actor);
    bool IsPickedByHand(RE::Actor* a_actor);

    std::string GetShownSkinName(RE::Actor* a_actor);

    void SetSkinByHand(RE::Actor* a_actor, const std::string& a_skin);

    void Reroll(RE::Actor* a_actor);

    std::size_t RerollAll(const std::vector<RE::Actor*>& a_loaded);

    void ApplyCurrent(RE::Actor* a_actor);

    void ApplyToLoadedActors(std::string_view a_source);

    enum class RuleMiss {
        kNone,
        kRuleOff,
        kOff,     // disableSkinDistribution
        kPlayer,  // the player follows no rules but the ones that name them
        kSex,
        kRace,
        kClass,
        kFaction,
        kPlugin,
        kNpc,
        kWeight,
        kVampire,
        kPreset,
        kNoSkin  // none of its skins fits their sex and body
    };

    RuleMiss CheckRule(const Parser::Rule& a_rule, RE::Actor* a_actor);

    struct FieldSource {
        enum class Kind {
            kGame,      // the game's texture
            kHandPick,  // the skin picked in the menu
            kRule
        };

        Kind kind{Kind::kGame};
        const Packs::Pack* pack{nullptr};
        std::string rule;
        std::string skin;
        bool random{false};
    };

    struct RuleUse {
        std::string name;
        std::string skin;
        bool used{false};
        bool random{false};
    };

    struct Resolved {
        Mix mix;
        bool any{false};
        const Packs::Pack* base{nullptr};
        FieldSource baseSource;
        std::array<FieldSource, kFieldTotal> fields;
        std::vector<RuleUse> rules;
        bool byHand{false};
    };

    Resolved Resolve(RE::Actor* a_actor, const std::optional<std::string>& a_skin);

    void SaveState(SKSE::SerializationInterface* a_save);
    void LoadState(SKSE::SerializationInterface* a_load);
    void RevertState(SKSE::SerializationInterface* a_revert);
}  // namespace Skin::Distribution
