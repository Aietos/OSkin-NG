#include "Skin/Body.h"

#include "STL.h"

namespace {
    // Keyed by skin and race form IDs, one map per sex since each sex has its own mesh
    std::mutex g_ubeBodiesLock;
    std::array<std::unordered_map<std::uint64_t, bool>, RE::SEXES::kTotal> g_ubeBodies;

    bool IsUBEMesh(const char* a_model) {
        return a_model && (stl::contains(a_model, "!UBE\\"sv) || stl::contains(a_model, "!UBE/"sv));
    }
}  // namespace

bool Skin::IsUBE(RE::Actor* a_actor) {
    const auto* const base{a_actor->GetActorBase()};
    auto* const race{a_actor->GetRace()};

    if (!base || !race) return false;

    const auto* const skin{base->skin ? base->skin : race->skin};

    if (!skin) return false;

    const auto sex{base->GetSex() == RE::SEX::kFemale ? RE::SEXES::kFemale : RE::SEXES::kMale};
    const std::uint64_t key{(static_cast<std::uint64_t>(skin->GetFormID()) << 32) | race->GetFormID()};

    {
        std::lock_guard lock{g_ubeBodiesLock};

        if (const auto known{g_ubeBodies[sex].find(key)}; known != g_ubeBodies[sex].end()) return known->second;
    }

    bool ube{false};

    for (const auto* const addon : skin->armorAddons) {
        if (addon && addon->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) && addon->IsValidRace(race)) {
            ube = IsUBEMesh(addon->bipedModels[sex].GetModel());
            break;
        }
    }

    std::lock_guard lock{g_ubeBodiesLock};

    if (g_ubeBodies[sex].emplace(key, ube).second && ube) {
        logger::info("Skin {:08X} on race {} has a UBE body, only UBE skins are given to it", skin->GetFormID(),
                     stl::get_editorID(race));
    }

    return ube;
}
