#pragma once

namespace Parser {
    constexpr auto InGameConfigFileName{"~ZZZOSkin_Ingame.json"};

    struct IncludeFileInfo {
        std::string name;
        std::string from;
        bool skipped{false};
    };

    std::optional<std::string> LoadConfig(rapidjson::Document& a_config, std::vector<IncludeFileInfo>& a_includeFiles);

    std::optional<std::string> SetMasterConfigValue(const char* a_key, bool a_value);
    std::optional<std::string> SetMasterConfigValue(const char* a_key, std::string_view a_value);
    std::optional<std::string> SetMasterConfigNumber(const char* a_key, std::int64_t a_value);

    std::optional<std::string> SaveNpcRule(std::string_view a_name, std::string_view a_plugin,
                                           std::string_view a_formID, std::string_view a_skin);

    std::optional<std::string> RemoveRule(std::string_view a_name);

    struct MixParts {
        std::string base;
        std::string diffuse;
        std::string normal;
        std::string subsurface;
        std::string specular;
        std::string head;
        std::string headNormal;
        std::string teeth;
    };

    std::optional<std::string> SaveMix(std::string_view a_name, const MixParts& a_parts);

    struct EntryRef {
        std::string key;
        std::string plugin;
        std::string entry;
    };

    using EntrySources = std::unordered_map<std::string, std::string>;

    [[nodiscard]] std::string EntrySourceId(const EntryRef& a_ref);

    std::optional<std::string> MergeConfigFiles(rapidjson::Document& a_config, EntrySources& a_sources,
                                                const rapidjson::Document* a_inGame);

    std::optional<std::string> ReadInGameConfig(rapidjson::Document& a_config, std::string& a_text);

    std::optional<std::string> ValidateInGameConfig(rapidjson::Document& a_config);

    std::optional<std::string> WriteInGameConfig(const rapidjson::Document& a_config, std::string_view a_readText);

    namespace InGameEdit {
        void SetEntry(rapidjson::Document& a_config, const EntryRef& a_ref, const std::vector<std::string>* a_skins);
        void SetEntryValue(rapidjson::Document& a_config, const EntryRef& a_ref, const rapidjson::Value& a_value,
                           bool a_first = false);
        bool DeleteEntry(rapidjson::Document& a_config, const EntryRef& a_ref);

        void AddRemoval(rapidjson::Document& a_config, const EntryRef& a_ref);
        bool DeleteRemoval(rapidjson::Document& a_config, const EntryRef& a_ref);
    }  // namespace InGameEdit
}  // namespace Parser
