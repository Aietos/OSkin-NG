#pragma once

#include "Config/ConfigLoader.h"
#include "Skin/Apply.h"

namespace Parser {
    inline constexpr std::string_view AllSkins{"*"};

    inline constexpr std::string_view RandomRuleName{"Everyone else"};

    inline constexpr auto RandomBlacklistKey{"blacklistedSkinsFromRandomDistribution"};

    struct Rule {
        std::string name;
        bool wholeSkin{true};
        std::array<bool, Skin::kFieldTotal> parts{};
        std::optional<bool> female;
        std::vector<std::string> races;
        std::vector<std::string> classes;
        std::vector<std::string> factions;
        std::vector<std::string> plugins;
        std::unordered_set<RE::FormID> npcs;
        bool byNpc{false};
        std::optional<std::pair<float, float>> weight;
        std::optional<bool> vampire;
        std::vector<std::string> presets;
        std::vector<std::string> skins;
        bool allSkins{false};
        std::vector<std::string> exceptSkins;
        bool gameSkin{false};
        bool off{false};
        bool builtIn{false};

        [[nodiscard]] bool Sets(const Skin::Field a_field) const { return wholeSkin || parts[a_field]; }
    };

    Rule ParseRule(std::string_view a_name, const rapidjson::Value& a_value);

    class Config {
    public:
        Config() { document.SetObject(); }

        Config(Config&&) = delete;
        Config(const Config&) = delete;

        Config& operator=(Config&&) = delete;
        Config& operator=(const Config&) = delete;

        void Process();

        [[nodiscard]] const Skin::Mix* FindSkin(std::string_view a_name) const;

        [[nodiscard]] bool CanGiveSkin(const Skin::Mix& a_skin, bool a_female, bool a_ube) const;

        [[nodiscard]] static bool IsUBESkin(const Skin::Mix& a_skin);

        [[nodiscard]] static bool HasBodyColor(const Skin::Mix& a_skin, bool a_female);

        [[nodiscard]] std::vector<const Skin::Mix*> GetAllSkins() const;

        [[nodiscard]] std::vector<const Skin::Mix*> GetMenuSkins(bool a_female, bool a_ube) const;

        [[nodiscard]] bool IsLeftOutOfAllSkins(std::string_view a_name) const;

        [[nodiscard]] const std::vector<Rule>& GetRules() const { return rules; }

        [[nodiscard]] const Rule* FindRule(std::string_view a_name) const;

        [[nodiscard]] bool UsesPresets() const { return usesPresets; }

        [[nodiscard]] static bool IsNPCFromPlugin(const RE::TESNPC* a_npc, std::string_view a_plugin);

        [[nodiscard]] std::uint32_t GetMenuHotkey() const;

        [[nodiscard]] bool IsDistributionDisabled(const bool a_female) const {
            return a_female ? distributionDisabledForFemale : distributionDisabledForMale;
        }

        [[nodiscard]] bool DoesPlayerFollowRules() const { return playerFollowsRules; }

        rapidjson::Document document;

        std::vector<IncludeFileInfo> includeFiles;

        std::uint32_t generation{};

    private:
        void ProcessDisableSkinDistribution();
        void ProcessSkins();
        void ProcessRules();
        void AddRandomRule();

        bool distributionDisabledForFemale{};
        bool distributionDisabledForMale{};
        bool playerFollowsRules{false};
        bool usesPresets{false};

        std::unordered_map<std::string, Skin::Mix> skins;

        std::vector<Rule> rules;
    };

    class JSONParser {
    public:
        JSONParser(JSONParser&&) = delete;
        JSONParser(const JSONParser&) = delete;

        JSONParser& operator=(JSONParser&&) = delete;
        JSONParser& operator=(const JSONParser&) = delete;

        static JSONParser& GetInstance();

        // Take it once and use the same one for the whole lookup, a reload can replace it at any time
        [[nodiscard]] std::shared_ptr<const Config> GetConfig() const { return config_.load(); }

        struct LoadResult {
            std::optional<std::string> error;
            std::vector<std::string> skippedFiles;
        };

        LoadResult Load();

    private:
        JSONParser() = default;
        static JSONParser instance;

        std::atomic<std::shared_ptr<const Config>> config_{std::make_shared<const Config>()};
        std::uint32_t loadedConfigs_{0};
    };
}  // namespace Parser
