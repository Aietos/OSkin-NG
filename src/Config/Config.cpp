#include "Config/Config.h"

#include "STL.h"
#include "Skin/PackReport.h"

Parser::JSONParser Parser::JSONParser::instance;

namespace {
    constexpr auto TitleFormatSpecifier{"{:-^100}"};

    std::string Lowercase(const std::string_view a_text) {
        std::string lower{a_text};
        std::ranges::transform(lower, lower.begin(), [](const char c) { return stl::to_lower(c); });

        return lower;
    }

    std::string_view View(const rapidjson::Value& a_value) { return {a_value.GetString(), a_value.GetStringLength()}; }

    bool IsActorInForm(const RE::TESNPC* a_npc, const std::string_view a_plugin) {
        if (!a_npc->sourceFiles.array) return false;

        for (const auto* const file : *a_npc->sourceFiles.array) {
            if (file && file->fileName == a_plugin) return true;
        }

        return false;
    }

    std::string DiscardFormDigits(const std::string_view a_formID, const RE::TESFile* a_mod) {
        char temp[9]{"00000000"};
        std::memcpy(temp + (8 - a_formID.length()), a_formID.data(), a_formID.length());

        return std::string(temp + (a_mod->IsLight() ? 5 : 2));
    }

    RE::FormID LookupFormID(RE::TESDataHandler* a_dataHandler, const RE::TESFile* a_mod,
                            const std::string_view a_plugin, const std::string_view a_formID) {
        const auto local{DiscardFormDigits(a_formID, a_mod)};
        std::uint32_t id{0};

        std::from_chars(local.data(), local.data() + local.size(), id, 16);

        const auto* const form{a_dataHandler->LookupForm(id, a_plugin)};

        return form ? form->GetFormID() : 0;
    }

    std::vector<std::string> ToStrings(const rapidjson::Value& a_list) {
        std::vector<std::string> strings;

        if (!a_list.IsArray()) return strings;

        for (const auto& item : a_list.GetArray()) {
            if (item.IsString()) strings.emplace_back(View(item));
        }

        return strings;
    }

    bool HasName(const std::vector<std::string>& a_names, const std::string_view a_name) {
        return std::ranges::any_of(a_names, [=](const std::string& a_listed) { return stl::cmp(a_listed, a_name); });
    }
}  // namespace

Parser::JSONParser& Parser::JSONParser::GetInstance() { return instance; }

Parser::JSONParser::LoadResult Parser::JSONParser::Load() {
    auto config{std::make_shared<Config>()};
    LoadResult result;

    if (auto error{LoadConfig(config->document, config->includeFiles)}) {
        logger::error("The config wasn't loaded: {}", *error);
        result.error = std::move(error);

        return result;
    }

    config->Process();
    config->generation = ++loadedConfigs_;

    for (const auto& file : config->includeFiles) {
        if (file.skipped) {
            result.skippedFiles.push_back(file.name);
        }
    }

    config_.store(std::move(config));
    logger::info("Config {} is now in use", loadedConfigs_);

    return result;
}

void Parser::Config::Process() {
    [[maybe_unused]] stl::timeit const t;

    ProcessDisableSkinDistribution();

    if (const auto key{document.FindMember("playerFollowsRules")}; key != document.MemberEnd() && key->value.IsBool()) {
        playerFollowsRules = key->value.GetBool();
    }

    ProcessSkins();
    ProcessRules();
    AddRandomRule();
}

Parser::Rule Parser::ParseRule(const std::string_view a_name, const rapidjson::Value& a_value) {
    Rule rule{.name = std::string{a_name}};

    if (!a_value.IsObject()) return rule;

    const auto member = [&](const char* a_key) -> const rapidjson::Value* {
        const auto found{a_value.FindMember(a_key)};

        return found == a_value.MemberEnd() ? nullptr : &found->value;
    };

    if (const auto* const parts{member("parts")}) {
        rule.wholeSkin = false;

        for (const auto& part : ToStrings(*parts)) {
            if (const auto field{std::ranges::find(Skin::FieldNames, part)}; field != Skin::FieldNames.end()) {
                rule.parts[static_cast<std::size_t>(field - Skin::FieldNames.begin())] = true;
            }
        }
    }

    if (const auto* const sex{member("sex")}; sex && sex->IsString()) {
        rule.female = View(*sex) == "female";
    }

    for (auto& [key, list] : {std::pair{"race", &rule.races},
                              {"class", &rule.classes},
                              {"faction", &rule.factions},
                              {"plugin", &rule.plugins},
                              {"obodyPreset", &rule.presets},
                              {"skins", &rule.skins},
                              {"exceptSkins", &rule.exceptSkins}}) {
        if (const auto* const values{member(key)}) {
            *list = ToStrings(*values);
        }
    }

    if (const auto all{std::ranges::find(rule.skins, AllSkins)}; all != rule.skins.end()) {
        rule.allSkins = true;
        std::erase(rule.skins, std::string{AllSkins});
    }

    if (const auto* const npcs{member("npcFormID")}; npcs && npcs->IsObject()) {
        auto* const dataHandler{RE::TESDataHandler::GetSingleton()};

        for (const auto& [plugin, formIDs] : npcs->GetObj()) {
            const auto* const mod{dataHandler->LookupModByName(View(plugin))};

            rule.byNpc = true;

            if (!mod || !formIDs.IsArray()) continue;

            for (const auto& formID : formIDs.GetArray()) {
                if (!formID.IsString()) continue;

                if (const auto id{LookupFormID(dataHandler, mod, View(plugin), View(formID))}; id != 0) {
                    rule.npcs.insert(id);
                }
            }
        }
    }

    if (const auto* const weight{member("weight")};
        weight && weight->IsArray() && weight->Size() == 2 && (*weight)[0].IsNumber() && (*weight)[1].IsNumber()) {
        rule.weight = std::pair{(*weight)[0].GetFloat(), (*weight)[1].GetFloat()};
    }

    for (auto& [key, flag] : {std::pair{"gameSkin", &rule.gameSkin}, {"off", &rule.off}}) {
        if (const auto* const value{member(key)}; value && value->IsBool()) {
            *flag = value->GetBool();
        }
    }

    if (const auto* const vampire{member("vampire")}; vampire && vampire->IsBool()) {
        rule.vampire = vampire->GetBool();
    }

    return rule;
}

void Parser::Config::ProcessRules() {
    logger::info(TitleFormatSpecifier, "skinRules");

    const auto key{document.FindMember("skinRules")};

    if (key == document.MemberEnd() || !key->value.IsObject()) return;

    for (const auto& [name, value] : key->value.GetObj()) {
        auto rule{ParseRule(View(name), value)};

        for (const auto& skin : rule.skins) {
            if (!FindSkin(skin)) {
                logger::warn("The rule {} lists the skin {}, but there's no such pack or mix", rule.name, skin);
            }
        }

        if (!rule.wholeSkin && std::ranges::none_of(rule.parts, std::identity{})) {
            logger::warn("The rule {} sets no part, it does nothing", rule.name);
        }

        if (!rule.gameSkin && !rule.allSkins && rule.skins.empty()) {
            logger::warn("The rule {} gives no skin, it does nothing", rule.name);
        }

        if (!rule.off && !rule.presets.empty()) {
            usesPresets = true;
        }

        logger::info("Rule {}: {}{}{}", rule.name, rule.wholeSkin ? "whole skin" : "some parts",
                     rule.gameSkin
                         ? ", game skin"
                         : std::format(", {} skins{}", rule.skins.size(), rule.allSkins ? " and all skins" : ""),
                     rule.off ? ", off" : "");
        rules.push_back(std::move(rule));
    }
}

void Parser::Config::AddRandomRule() {
    if (FindRule(RandomRuleName)) {
        logger::info("The rule {} takes the place of the random skins", RandomRuleName);

        return;
    }

    Rule rule{.name = std::string{RandomRuleName}, .allSkins = true, .builtIn = true};

    if (const auto key{document.FindMember(RandomBlacklistKey)}; key != document.MemberEnd()) {
        rule.exceptSkins = ToStrings(key->value);
    }

    for (const auto& skin : rule.exceptSkins) {
        if (!FindSkin(skin)) {
            logger::warn("{} lists the skin {}, but there's no such pack or mix", RandomBlacklistKey, skin);
        }
    }

    logger::info("NPCs get random skins for what no rule sets, {} skins left out", rule.exceptSkins.size());
    rules.push_back(std::move(rule));
}

void Parser::Config::ProcessDisableSkinDistribution() {
    const auto key{document.FindMember("disableSkinDistribution")};

    if (key == document.MemberEnd() || !key->value.IsString()) return;

    const auto value{View(key->value)};

    if (value == "male") {
        distributionDisabledForMale = true;
    } else if (value == "female") {
        distributionDisabledForFemale = true;
    } else if (value == "all") {
        distributionDisabledForMale = true;
        distributionDisabledForFemale = true;
    } else if (value != "none") {
        logger::warn("Unknown disableSkinDistribution value '{}', treating as 'none'", value);
    }
}

void Parser::Config::ProcessSkins() {
    logger::info(TitleFormatSpecifier, "skins");

    for (const auto& pack : Skin::Packs::GetAll()) {
        skins.insert_or_assign(Lowercase(pack.name), Skin::Mix{.name = pack.name, .base = &pack});
    }

    if (const auto key{document.FindMember("skinMixes")}; key != document.MemberEnd() && key->value.IsObject()) {
        for (const auto& [nameValue, fields] : key->value.GetObj()) {
            const auto name{View(nameValue)};

            if (Skin::Packs::FindPack(name)) {
                logger::warn("The mix {} has a pack's name, it's ignored", name);
                continue;
            }

            const auto pack = [&](const char* a_field) -> const Skin::Packs::Pack* {
                const auto field{fields.FindMember(a_field)};

                if (field == fields.MemberEnd() || !field->value.IsString()) return nullptr;

                const auto* const found{Skin::Packs::FindPack(View(field->value))};

                if (!found) {
                    logger::warn("The mix {} uses the pack {} as its {}, but there's no such pack", name,
                                 View(field->value), a_field);
                }

                return found;
            };

            Skin::Mix mix{.name = std::string{name}, .base = pack("base")};

            if (!mix.base) {
                logger::warn("The mix {} has no base pack, it's ignored", name);
                continue;
            }

            mix.diffuse = pack("diffuse");
            mix.normal = pack("normal");
            mix.subsurface = pack("subsurface");
            mix.specular = pack("specular");
            mix.head = pack("head");
            mix.headNormal = pack("headNormal");
            mix.teeth = pack("teeth");

            logger::info("Mix {} on base {}", name, mix.base->name);
            skins.insert_or_assign(Lowercase(name), std::move(mix));
        }
    }

    logger::info("{} skins, packs and mixes together", skins.size());
}

const Skin::Mix* Parser::Config::FindSkin(const std::string_view a_name) const {
    const auto skin{skins.find(Lowercase(a_name))};

    return skin == skins.end() ? nullptr : &skin->second;
}

const Parser::Rule* Parser::Config::FindRule(const std::string_view a_name) const {
    const auto rule{std::ranges::find(rules, a_name, &Rule::name)};

    return rule == rules.end() ? nullptr : &*rule;
}

bool Parser::Config::IsUBESkin(const Skin::Mix& a_skin) { return a_skin.base->ube; }

bool Parser::Config::CanGiveSkin(const Skin::Mix& a_skin, const bool a_female, const bool a_ube) const {
    if (!(a_female ? a_skin.base->female : a_skin.base->male)) return false;

    // Players have one body installed, so only UBE is checked per NPC
    return IsUBESkin(a_skin) == a_ube;
}

bool Parser::Config::HasBodyColor(const Skin::Mix& a_skin, const bool a_female) {
    const auto* const pack{a_skin.diffuse ? a_skin.diffuse : a_skin.base};
    const auto& report{Skin::PackReport::Get(*pack)};

    return (a_female ? report.female : report.male).cells[Skin::PackReport::kBody][Skin::PackReport::kDiffuse].Has();
}

bool Parser::Config::IsNPCFromPlugin(const RE::TESNPC* a_npc, const std::string_view a_plugin) {
    return IsActorInForm(a_npc, a_plugin);
}

bool Parser::Config::IsLeftOutOfAllSkins(const std::string_view a_name) const {
    return std::ranges::any_of(
        rules, [=](const Rule& a_rule) { return a_rule.allSkins && HasName(a_rule.exceptSkins, a_name); });
}

std::vector<const Skin::Mix*> Parser::Config::GetMenuSkins(const bool a_female, const bool a_ube) const {
    const auto show{document.FindMember("blacklistedSkinsShowInMenu")};
    const bool showLeftOut{show == document.MemberEnd() || !show->value.IsBool() || show->value.GetBool()};
    std::vector<const Skin::Mix*> menuSkins;

    for (const auto& skin : skins | std::views::values) {
        if (!CanGiveSkin(skin, a_female, a_ube)) continue;

        if (!showLeftOut && IsLeftOutOfAllSkins(skin.name)) continue;

        menuSkins.push_back(&skin);
    }

    return menuSkins;
}

std::uint32_t Parser::Config::GetMenuHotkey() const {
    const auto key{document.FindMember("menuHotkey")};

    if (key == document.MemberEnd() || !key->value.IsUint()) return 0x25;

    return key->value.GetUint();
}

std::vector<const Skin::Mix*> Parser::Config::GetAllSkins() const {
    std::vector<const Skin::Mix*> all;

    for (const auto& skin : skins | std::views::values) {
        all.push_back(&skin);
    }

    return all;
}
