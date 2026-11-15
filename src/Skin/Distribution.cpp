#include "Skin/Distribution.h"

#include "API/Links.h"
#include "Config/Config.h"
#include "Preview/Preview.h"
#include "STL.h"
#include "Skin/Body.h"

namespace {
    constexpr std::uint32_t ActorsRecord{'ACTS'};
    constexpr std::uint32_t ActorsVersion{2};

    // What the save keeps of an actor. Rule picks come from the config, so they aren't kept
    struct State {
        std::optional<std::string> handPick;
        std::uint32_t seed{0};
    };

    // Skins are applied on the main thread and on the threads that load NPCs at the same time
    std::mutex g_lock;
    std::unordered_map<RE::FormID, State> g_states;
    std::uint32_t g_globalSeed{0};
    std::unordered_map<RE::FormID, std::string> g_shownNames;

    // The mix the menu is making. The copy's skin hooks may read it from the threads that load NPCs
    constexpr std::string_view DraftPrefix{"<OSkin draft "};
    std::mutex g_draftLock;
    Skin::Mix g_draft;
    std::uint32_t g_draftCount{0};

    bool IsTarget(RE::Actor* a_actor) {
        return a_actor->GetActorBase() && a_actor->HasKeywordString("ActorTypeNPC") && !a_actor->IsChild();
    }

    // A leveled actor's temporary base can't be named, so the leveled pick and the placed record follow
    std::vector<const RE::TESNPC*> GetLookupNPCs(RE::Actor* a_actor) {
        std::vector<const RE::TESNPC*> npcs;

        const auto addWithTemplates = [&](const RE::TESForm* a_form) {
            for (int depth{0}; a_form && depth < 16; ++depth) {
                const auto* const npc{a_form->As<RE::TESNPC>()};

                if (!npc) break;

                if (std::ranges::find(npcs, npc) == npcs.end()) npcs.push_back(npc);

                if (npc->actorData.templateUseFlags.none(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kTraits)) break;

                a_form = npc->baseTemplateForm;
            }
        };

        addWithTemplates(a_actor->GetActorBase());

        if (const auto* const leveled{a_actor->extraList.GetByType<RE::ExtraLeveledCreature>()}) {
            addWithTemplates(leveled->templateBase);
            addWithTemplates(leveled->originalBase);
        }

        return npcs;
    }

    std::uint64_t HashBytes(std::uint64_t a_hash, const void* a_data, const std::size_t a_size) {
        const auto* const bytes{static_cast<const std::uint8_t*>(a_data)};

        for (std::size_t i = 0; i < a_size; ++i) {
            a_hash ^= bytes[i];
            a_hash *= 0x100000001b3;
        }

        return a_hash;
    }

    std::uint64_t HashText(const std::uint64_t a_hash, const std::string_view a_text) {
        return HashBytes(a_hash, a_text.data(), a_text.size());
    }

    // Spreads the bits, so close inputs give scores far apart
    std::uint64_t Mix64(std::uint64_t a_value) {
        a_value ^= a_value >> 30;
        a_value *= 0xbf58476d1ce4e5b9;
        a_value ^= a_value >> 27;
        a_value *= 0x94d049bb133111eb;
        a_value ^= a_value >> 31;

        return a_value;
    }

    // The same in every save and load order, a placed actor by plugin and ID, one made in game by FormID
    std::uint64_t ActorKey(RE::Actor* a_actor) {
        constexpr std::uint64_t start{0xcbf29ce484222325};

        if (const auto* const file{a_actor->IsDynamicForm() ? nullptr : a_actor->GetFile(0)}) {
            const auto local{a_actor->GetLocalFormID()};

            return HashBytes(HashText(start, file->GetFilename()), &local, sizeof(local));
        }

        const auto formID{a_actor->GetFormID()};

        return HashBytes(start, &formID, sizeof(formID));
    }

    struct Subject {
        std::uint64_t key{0};
        std::uint32_t seed{0};
        const RE::TESNPC* base{nullptr};
        bool female{false};
        bool player{false};
        bool ube{false};
        bool vampire{false};
        float weight{0.0f};
        std::string race;
        std::string actorClass;
        std::string preset;
        std::vector<const RE::TESNPC*> lookupNPCs;
        std::vector<RE::FormID> lookupIDs;
    };

    Subject MakeSubject(RE::Actor* a_actor, const bool a_presets) {
        auto* const base{a_actor->GetActorBase()};
        Subject subject{.key = ActorKey(a_actor), .base = base, .player = a_actor->IsPlayerRef()};

        if (!base) return subject;

        {
            std::lock_guard lock{g_lock};

            const auto state{g_states.find(a_actor->GetFormID())};
            subject.seed = g_globalSeed ^ (state != g_states.end() ? state->second.seed : 0);
        }

        subject.female = base->GetSex() == RE::SEX::kFemale;
        subject.ube = Skin::IsUBE(a_actor);
        subject.weight = base->weight;
        subject.race = a_actor->GetRace() ? stl::get_editorID(a_actor->GetRace()) : std::string{};
        subject.actorClass = base->npcClass ? stl::get_editorID(base->npcClass) : std::string{};
        subject.lookupNPCs = GetLookupNPCs(a_actor);

        for (const auto* const npc : subject.lookupNPCs) {
            subject.lookupIDs.push_back(npc->GetFormID());
        }

        if (a_presets) {
            subject.preset = API::OBody::GetPreset(a_actor);
        }

        // Vampire races have the keyword, some vampires only have the faction
        static const auto* const vampireFaction{RE::TESForm::LookupByEditorID<RE::TESFaction>("DLC1VampireFaction")};

        subject.vampire = a_actor->HasKeywordString("Vampire") ||
                          (vampireFaction && std::ranges::any_of(base->factions, [](const RE::FACTION_RANK& a_rank) {
                               return a_rank.faction == vampireFaction;
                           }));

        return subject;
    }

    bool HasName(const std::vector<std::string>& a_names, const std::string_view a_name) {
        return std::ranges::any_of(a_names, [=](const std::string& a_listed) { return stl::cmp(a_listed, a_name); });
    }

    bool HasMouthFiles(const Skin::Mix& a_skin) {
        const auto* const pack{a_skin.teeth ? a_skin.teeth : a_skin.base};

        return std::ranges::any_of(pack->textures | std::views::keys,
                                   [](const std::string& a_key) { return a_key.starts_with("mouth\\"); });
    }

    // Picks by score, so a skin added to a rule only takes the actors it now wins
    // Teeth are the same for every sex and body, so a teeth rule takes any skin with mouth files
    const Skin::Mix* PickRuleSkin(const Parser::Rule& a_rule, const Subject& a_subject,
                                  const Parser::Config& a_config) {
        const bool teethOnly{!a_rule.wholeSkin && std::ranges::count(a_rule.parts, true) == 1 &&
                             a_rule.parts[Skin::kTeeth]};
        const Skin::Mix* best{nullptr};
        std::uint64_t bestScore{0};

        const auto consider = [&](const Skin::Mix& a_skin) {
            if (teethOnly ? !HasMouthFiles(a_skin) : !a_config.CanGiveSkin(a_skin, a_subject.female, a_subject.ube)) {
                return;
            }

            auto hash{HashBytes(0xcbf29ce484222325, &a_subject.key, sizeof(a_subject.key))};
            hash = HashBytes(hash, &a_subject.seed, sizeof(a_subject.seed));
            hash = HashText(hash, a_rule.name);
            hash = HashText(hash, a_skin.name);

            if (const auto score{Mix64(hash)}; !best || score > bestScore) {
                best = &a_skin;
                bestScore = score;
            }
        };

        for (const auto& name : a_rule.skins) {
            if (const auto* const skin{a_config.FindSkin(name)}) {
                consider(*skin);
            }
        }

        if (a_rule.allSkins) {
            for (const auto* const skin : a_config.GetAllSkins()) {
                if (Parser::Config::HasBodyColor(*skin, a_subject.female) && !HasName(a_rule.exceptSkins, skin->name) &&
                    !HasName(a_rule.skins, skin->name)) {
                    consider(*skin);
                }
            }
        }

        return best;
    }

    Skin::Distribution::RuleMiss Check(const Parser::Rule& a_rule, const Subject& a_subject,
                                       const Parser::Config& a_config) {
        using Miss = Skin::Distribution::RuleMiss;

        const auto namesNpc{[&] {
            return std::ranges::any_of(a_subject.lookupIDs,
                                       [&](const RE::FormID a_formID) { return a_rule.npcs.contains(a_formID); });
        }};

        if (a_rule.off) return Miss::kRuleOff;

        if (!a_subject.base || a_config.IsDistributionDisabled(a_subject.female)) return Miss::kOff;

        if (a_subject.player && !a_config.DoesPlayerFollowRules() && !(a_rule.byNpc && namesNpc())) {
            return Miss::kPlayer;
        }

        if (a_rule.female && *a_rule.female != a_subject.female) return Miss::kSex;

        if (!a_rule.races.empty() && !HasName(a_rule.races, a_subject.race)) return Miss::kRace;

        if (!a_rule.classes.empty() && !HasName(a_rule.classes, a_subject.actorClass)) return Miss::kClass;

        if (!a_rule.factions.empty()) {
            const bool inFaction{std::ranges::any_of(a_rule.factions, [&](const std::string& a_faction) {
                const auto* const form{RE::TESForm::LookupByEditorID<RE::TESFaction>(a_faction)};

                return form && std::ranges::any_of(a_subject.base->factions, [=](const RE::FACTION_RANK& a_rank) {
                           return a_rank.faction == form;
                       });
            })};

            if (!inFaction) return Miss::kFaction;
        }

        if (!a_rule.plugins.empty()) {
            const bool fromPlugin{std::ranges::any_of(a_rule.plugins, [&](const std::string& a_plugin) {
                return std::ranges::any_of(a_subject.lookupNPCs, [&](const RE::TESNPC* a_npc) {
                    return Parser::Config::IsNPCFromPlugin(a_npc, a_plugin);
                });
            })};

            if (!fromPlugin) return Miss::kPlugin;
        }

        if (a_rule.byNpc && !namesNpc()) return Miss::kNpc;

        if (a_rule.weight && (a_subject.weight < a_rule.weight->first || a_subject.weight > a_rule.weight->second)) {
            return Miss::kWeight;
        }

        if (a_rule.vampire && *a_rule.vampire != a_subject.vampire) return Miss::kVampire;

        if (!a_rule.presets.empty() && (a_subject.preset.empty() || !HasName(a_rule.presets, a_subject.preset))) {
            return Miss::kPreset;
        }

        if (!a_rule.gameSkin && !PickRuleSkin(a_rule, a_subject, a_config)) return Miss::kNoSkin;

        return Miss::kNone;
    }

    RE::NiPointer<RE::Actor> RulesActor(RE::Actor* a_actor) {
        if (auto target{Preview::Session::GetInstance().GetRuleSubject(a_actor)}) return target;

        if (auto* const original{API::OBody::GetPreviewCopyTarget(a_actor)}) return RE::NiPointer<RE::Actor>{original};

        return RE::NiPointer<RE::Actor>{a_actor};
    }

    Skin::Distribution::Resolved ResolveHandPick(RE::Actor* a_actor, const std::string& a_skin,
                                                 const Parser::Config& a_config) {
        using Kind = Skin::Distribution::FieldSource::Kind;

        Skin::Distribution::Resolved resolved{.byHand = true};

        if (a_skin.starts_with(DraftPrefix)) {
            std::lock_guard lock{g_draftLock};

            if (g_draft.base) {
                resolved.mix = g_draft;

                // For the log, it may have no name yet
                if (resolved.mix.name.empty()) {
                    resolved.mix.name = "the mix being made";
                }
            }
        } else if (!a_skin.empty()) {
            if (const auto* const mix{a_config.FindSkin(a_skin)}) {
                resolved.mix = *mix;
            } else {
                logger::warn("{} has the skin {}, but there's no such pack or mix anymore", a_actor->GetName(), a_skin);
            }
        }

        resolved.base = resolved.mix.base;
        resolved.any = resolved.base != nullptr;

        if (!resolved.base) return resolved;

        resolved.baseSource = {.kind = Kind::kHandPick, .pack = resolved.base, .skin = resolved.mix.name};

        for (std::size_t field = 0; field < Skin::kFieldTotal; ++field) {
            const auto* const own{resolved.mix.Get(static_cast<Skin::Field>(field))};

            resolved.fields[field] = {
                .kind = Kind::kHandPick, .pack = own ? own : resolved.base, .skin = resolved.mix.name};
        }

        return resolved;
    }

    Skin::Distribution::Resolved ResolveRules(RE::Actor* a_actor, const Parser::Config& a_config) {
        using Kind = Skin::Distribution::FieldSource::Kind;

        Skin::Distribution::Resolved resolved;

        if (a_config.GetRules().empty()) return resolved;

        const auto rulesActor{RulesActor(a_actor)};
        const auto subject{MakeSubject(rulesActor.get(), a_config.UsesPresets())};
        std::array<bool, Skin::kFieldTotal> taken{};
        bool baseTaken{false};
        std::string baseSkin;
        std::string names;

        for (const auto& rule : a_config.GetRules()) {
            if (Check(rule, subject, a_config) != Skin::Distribution::RuleMiss::kNone) continue;

            const auto* const skin{rule.gameSkin ? nullptr : PickRuleSkin(rule, subject, a_config)};
            Skin::Distribution::RuleUse use{
                .name = rule.name, .skin = skin ? skin->name : std::string{}, .random = rule.builtIn};

            if (rule.wholeSkin && !baseTaken) {
                baseTaken = true;
                use.used = true;

                if (skin) {
                    resolved.base = skin->base;
                    baseSkin = skin->name;
                    resolved.baseSource = {.kind = Kind::kRule,
                                           .pack = skin->base,
                                           .rule = rule.name,
                                           .skin = skin->name,
                                           .random = rule.builtIn};
                } else {
                    resolved.baseSource = {.kind = Kind::kRule, .rule = rule.name, .random = rule.builtIn};
                }
            }

            for (std::size_t field = 0; field < Skin::kFieldTotal; ++field) {
                const auto part{static_cast<Skin::Field>(field)};

                if (!rule.Sets(part) || taken[field]) continue;

                taken[field] = true;
                use.used = true;

                if (!skin) {
                    resolved.mix.gameParts[field] = true;
                    resolved.fields[field] = {.kind = Kind::kRule, .rule = rule.name, .random = rule.builtIn};
                    continue;
                }

                const auto* const own{skin->Get(part)};
                const auto* const pack{own ? own : rule.wholeSkin ? nullptr : skin->base};

                resolved.mix.Get(part) = pack;
                resolved.fields[field] = {.kind = Kind::kRule,
                                          .pack = pack ? pack : skin->base,
                                          .rule = rule.name,
                                          .skin = skin->name,
                                          .random = rule.builtIn};
            }

            if (use.used) {
                names += (names.empty() ? "" : ", ") + rule.name;
            }

            resolved.rules.push_back(std::move(use));
        }

        const auto follow = [&](const Skin::Field a_part, const std::initializer_list<Skin::Field> a_next) {
            if (resolved.mix.Get(a_part) || resolved.mix.gameParts[a_part]) return;

            for (const auto next : a_next) {
                if (resolved.mix.Get(next)) {
                    resolved.fields[a_part] = resolved.fields[next];

                    return;
                }
            }
        };

        follow(Skin::kHead, {Skin::kDiffuse});
        follow(Skin::kHeadNormal, {Skin::kHead, Skin::kNormal});

        resolved.mix.base = resolved.base;
        resolved.any = resolved.base || std::ranges::any_of(resolved.fields, [](const auto& a_source) {
                           return a_source.pack != nullptr;
                       });

        if (!names.empty()) {
            resolved.mix.name = std::format("{} from the rules {}", baseSkin.empty() ? "parts" : baseSkin, names);
        }

        return resolved;
    }

    template <class T>
    void Append(std::vector<std::uint8_t>& a_buffer, const T& a_value) {
        const auto* const bytes{reinterpret_cast<const std::uint8_t*>(&a_value)};

        a_buffer.insert(a_buffer.end(), bytes, bytes + sizeof(T));
    }

    template <class T>
    bool Take(const std::vector<std::uint8_t>& a_buffer, std::size_t& a_offset, T& a_value) {
        if (a_offset + sizeof(T) > a_buffer.size()) return false;

        std::memcpy(&a_value, a_buffer.data() + a_offset, sizeof(T));
        a_offset += sizeof(T);

        return true;
    }

    // A seed that isn't 0, which is no re-roll
    std::uint32_t NewSeed() { return stl::random<std::uint32_t>(1, (std::numeric_limits<std::uint32_t>::max)()); }
}  // namespace

void Skin::Distribution::ApplyForActor(RE::Actor* a_actor, RE::NiAVObject* a_root, const std::string_view a_source) {
    if (!a_actor || !a_root || !IsTarget(a_actor)) return;

    // The preview shows its own skin on the actor it's on, and on their copy
    if (std::optional<std::string> shown; Preview::Session::GetInstance().GetSkinShownOn(a_actor, shown)) {
        ShowSkin(a_actor, a_root, shown, a_source);

        return;
    }

    // OBody's preview copy wears the skin of the NPC it copies, and gets none of its own
    if (auto* const original{API::OBody::GetPreviewCopyTarget(a_actor)}) {
        ShowSkin(a_actor, a_root, GetHandPick(original), a_source);

        return;
    }

    ShowSkin(a_actor, a_root, GetHandPick(a_actor), a_source);
}

void Skin::Distribution::ShowSkin(RE::Actor* a_actor, RE::NiAVObject* a_root, const std::optional<std::string>& a_skin,
                                  const std::string_view a_source) {
    const auto resolved{Resolve(a_actor, a_skin)};

    {
        std::lock_guard lock{g_lock};

        g_shownNames.insert_or_assign(a_actor->GetFormID(), resolved.base ? resolved.baseSource.skin : std::string{});
    }

    Apply(a_actor, a_root, resolved.any ? &resolved.mix : nullptr, a_source);
}

Skin::Distribution::Resolved Skin::Distribution::Resolve(RE::Actor* a_actor, const std::optional<std::string>& a_skin) {
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};

    return a_skin ? ResolveHandPick(a_actor, *a_skin, *config) : ResolveRules(a_actor, *config);
}

Skin::Distribution::RuleMiss Skin::Distribution::CheckRule(const Parser::Rule& a_rule, RE::Actor* a_actor) {
    const auto config{Parser::JSONParser::GetInstance().GetConfig()};
    const auto rulesActor{RulesActor(a_actor)};

    return Check(a_rule, MakeSubject(rulesActor.get(), !a_rule.presets.empty()), *config);
}

std::optional<std::string> Skin::Distribution::GetHandPick(RE::Actor* a_actor) {
    std::lock_guard lock{g_lock};

    const auto state{g_states.find(a_actor->GetFormID())};

    return state == g_states.end() ? std::nullopt : state->second.handPick;
}

bool Skin::Distribution::IsPickedByHand(RE::Actor* a_actor) { return GetHandPick(a_actor).has_value(); }

std::string Skin::Distribution::GetShownSkinName(RE::Actor* a_actor) {
    if (auto handPick{GetHandPick(a_actor)}) return *handPick;

    std::lock_guard lock{g_lock};

    const auto shown{g_shownNames.find(a_actor->GetFormID())};

    return shown == g_shownNames.end() ? std::string{} : shown->second;
}

void Skin::Distribution::SetSkinByHand(RE::Actor* a_actor, const std::string& a_skin) {
    {
        std::lock_guard lock{g_lock};

        g_states[a_actor->GetFormID()].handPick = a_skin;
    }

    logger::info("{} gets the skin {} from the menu", a_actor->GetName(), a_skin.empty() ? "of the game" : a_skin);
    ApplyCurrent(a_actor);
}

void Skin::Distribution::Reroll(RE::Actor* a_actor) {
    {
        std::lock_guard lock{g_lock};

        auto& state{g_states[a_actor->GetFormID()]};
        state.handPick.reset();
        state.seed = NewSeed();
    }

    logger::info("{} gets new picks from the rules", a_actor->GetName());
    ApplyCurrent(a_actor);
}

std::size_t Skin::Distribution::RerollAll(const std::vector<RE::Actor*>& a_loaded) {
    {
        std::lock_guard lock{g_lock};

        g_globalSeed = NewSeed();

        std::erase_if(g_states, [](auto& a_entry) {
            a_entry.second.seed = 0;

            return !a_entry.second.handPick;
        });
    }

    for (auto* const actor : a_loaded) {
        ApplyCurrent(actor);
    }

    logger::info("Re-rolled the skins of all NPCs, {} loaded ones change now", a_loaded.size());

    return a_loaded.size();
}

void Skin::Distribution::ApplyCurrent(RE::Actor* a_actor) {
    ApplyForActor(a_actor, a_actor->Get3D(false), "Menu");

    if (a_actor->IsPlayerRef()) {
        ApplyForActor(a_actor, a_actor->Get3D(true), "Menu (first person)");
    }
}

void Skin::Distribution::ApplyToLoadedActors(const std::string_view a_source) {
    auto* const processLists{RE::ProcessLists::GetSingleton()};

    if (processLists) {
        for (auto* const handles : {&processLists->highActorHandles, &processLists->middleHighActorHandles}) {
            for (const auto& handle : *handles) {
                if (const auto actor{handle.get()}) {
                    ApplyForActor(actor.get(), actor->Get3D(false), a_source);
                }
            }
        }
    }

    if (auto* const player{RE::PlayerCharacter::GetSingleton()}) {
        ApplyForActor(player, player->Get3D(false), a_source);
        ApplyForActor(player, player->Get3D(true), std::format("{} (first person)", a_source));
    }
}

void Skin::Distribution::SaveState(SKSE::SerializationInterface* a_save) {
    std::vector<std::uint8_t> buffer;

    {
        std::lock_guard lock{g_lock};

        Append(buffer, g_globalSeed);
        Append(buffer, static_cast<std::uint32_t>(g_states.size()));

        for (const auto& [formID, state] : g_states) {
            const std::string skin{state.handPick.value_or(std::string{})};
            const auto length{static_cast<std::uint16_t>(std::min<std::size_t>(skin.size(), 0xFFFF))};
            const std::uint8_t flags{state.handPick ? std::uint8_t{1} : std::uint8_t{0}};

            Append(buffer, formID);
            Append(buffer, flags);
            Append(buffer, state.seed);
            Append(buffer, length);
            buffer.insert(buffer.end(), skin.begin(), skin.begin() + length);
        }
    }

    if (!a_save->OpenRecord(ActorsRecord, ActorsVersion) ||
        !a_save->WriteRecordData(buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
        logger::error("Couldn't write the actors' skins to the save");
    }
}

void Skin::Distribution::LoadState(SKSE::SerializationInterface* a_load) {
    std::uint32_t type{0};
    std::uint32_t version{0};
    std::uint32_t length{0};

    while (a_load->GetNextRecordInfo(type, version, length)) {
        if (type != ActorsRecord) continue;

        // Version 1 kept every skin, the ones the rules picked too. Only the hand picks are kept from it
        if (version != 1 && version != ActorsVersion) {
            logger::warn("The save has actor skins of an unknown version {}, they're ignored", version);
            continue;
        }

        std::vector<std::uint8_t> buffer(length);
        std::size_t offset{0};
        std::uint32_t globalSeed{0};
        std::uint32_t count{0};
        std::uint32_t loaded{0};

        a_load->ReadRecordData(buffer.data(), length);

        if (version == ActorsVersion) {
            Take(buffer, offset, globalSeed);
        }

        Take(buffer, offset, count);

        {
            std::lock_guard lock{g_lock};

            g_globalSeed = globalSeed;
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID formID{0};
            std::uint8_t flags{0};
            std::uint32_t seed{0};
            std::uint16_t nameLength{0};

            if (!Take(buffer, offset, formID) || !Take(buffer, offset, flags) ||
                (version == ActorsVersion && !Take(buffer, offset, seed)) || !Take(buffer, offset, nameLength) ||
                offset + nameLength > buffer.size()) {
                logger::error("The actors' skins in the save are cut short, read {} of {}", i, count);
                break;
            }

            std::string skin{reinterpret_cast<const char*>(buffer.data() + offset), nameLength};
            RE::FormID resolved{0};

            offset += nameLength;

            if ((flags & 1) == 0 && seed == 0) continue;

            if (!a_load->ResolveFormID(formID, resolved)) continue;

            std::lock_guard lock{g_lock};

            auto& state{g_states[resolved]};
            state.seed = seed;

            if ((flags & 1) != 0) {
                state.handPick = std::move(skin);
            }

            ++loaded;
        }

        logger::info("Read the skins of {} actors from the save", loaded);
    }
}

void Skin::Distribution::RevertState(SKSE::SerializationInterface*) {
    std::lock_guard lock{g_lock};

    g_states.clear();
    g_shownNames.clear();
    g_globalSeed = 0;
}

std::string Skin::Distribution::SetDraftMix(const Mix& a_mix) {
    std::lock_guard lock{g_draftLock};

    g_draft = a_mix;

    return std::format("{}{}", DraftPrefix, ++g_draftCount);
}
