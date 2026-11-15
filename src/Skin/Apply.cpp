#include "Skin/Apply.h"

#include "STL.h"
#include "Skin/Loader.h"

namespace {
    using Texture = RE::BSTextureSet::Texture;

    // The tint mask (kMultilayer) is never swapped
    constexpr std::array swapSlots{Texture::kDiffuse, Texture::kNormal, Texture::kSubsurfaceTint, Texture::kDetailMap,
                                   Texture::kSpecular};

    std::string Describe(RE::Actor* a_actor) {
        const char* const name{a_actor->GetName()};

        return std::format("{} ({:08X})", name ? name : "", a_actor->GetFormID());
    }

    bool HasPath(const std::array<const char*, Texture::kTotal>& a_paths, const Texture a_slot) {
        return a_paths[a_slot] && *a_paths[a_slot];
    }

    // The game puts some face textures on the material without a path in the texture set, like vanilla NPCs' face
    // tint and the player's tint made in character creation. Reloading from the set would lose them
    void KeepDirectFaceTextures(RE::BSLightingShaderMaterialFacegen* a_new, RE::BSLightingShaderMaterialFacegen* a_old,
                                const std::array<const char*, Texture::kTotal>& a_paths) {
        a_new->tintTexture = a_old->tintTexture;

        if (!HasPath(a_paths, Texture::kDetailMap)) {
            a_new->detailTexture = a_old->detailTexture;
        }

        if (!HasPath(a_paths, Texture::kSubsurfaceTint)) {
            a_new->subsurfaceTexture = a_old->subsurfaceTexture;
        }
    }

    // The game's textures, so a swap can be undone. Each actor's part remembers its own, since a pack's female file
    // can stand in for any race folder. The first one seen for each key is the fallback
    std::mutex g_originalsLock;
    std::unordered_map<std::string, std::string> g_originals;
    std::unordered_map<RE::FormID, std::unordered_map<std::string, std::string>> g_partOriginals;

    std::string PartKey(const RE::BSGeometry* a_geometry, const std::string& a_packKey) {
        return std::format("{}|{}", a_geometry->name.c_str(), a_packKey);
    }

    void RememberOriginal(const std::string& a_key, const char* a_path) {
        std::lock_guard lock{g_originalsLock};

        g_originals.try_emplace(a_key, a_path);
    }

    void RememberSwap(const RE::FormID a_actor, const RE::BSGeometry* a_geometry, const std::string& a_packKey,
                      const std::string& a_original) {
        std::lock_guard lock{g_originalsLock};

        g_partOriginals[a_actor].insert_or_assign(PartKey(a_geometry, a_packKey), a_original);
    }

    std::string FindOriginal(const RE::FormID a_actor, const RE::BSGeometry* a_geometry, const std::string& a_key) {
        std::lock_guard lock{g_originalsLock};

        if (const auto actor{g_partOriginals.find(a_actor)}; actor != g_partOriginals.end()) {
            if (const auto part{actor->second.find(PartKey(a_geometry, a_key))}; part != actor->second.end()) {
                return part->second;
            }
        }

        const auto original{g_originals.find(a_key)};

        return original == g_originals.end() ? std::string{} : original->second;
    }

    Skin::Field ChannelField(const Texture a_slot) {
        switch (a_slot) {
            case Texture::kDiffuse:
                return Skin::kDiffuse;
            case Texture::kNormal:
                return Skin::kNormal;
            case Texture::kSubsurfaceTint:
                return Skin::kSubsurface;
            case Texture::kSpecular:
                return Skin::kSpecular;
            default:
                return Skin::kFieldTotal;
        }
    }

    struct Found {
        const Skin::Packs::Pack* pack{nullptr};
        const std::string* texture{nullptr};
    };

    Found FindTexture(const Skin::Mix& a_mix, const Texture a_slot, const bool a_face, const std::string& a_key) {
        const bool mouth{a_key.starts_with("mouth\\")};
        const std::array fields{a_face && a_slot == Texture::kNormal ? Skin::kHeadNormal : Skin::kFieldTotal,
                                a_face ? Skin::kHead : Skin::kFieldTotal, mouth ? Skin::kTeeth : Skin::kFieldTotal,
                                ChannelField(a_slot)};

        for (const auto field : fields) {
            if (field == Skin::kFieldTotal) continue;

            if (a_mix.gameParts[field]) return {};

            if (const auto* const pack{a_mix.Get(field)}) {
                if (const auto* const texture{Skin::Packs::Find(*pack, a_key)}) return {pack, texture};
            }
        }

        if (a_mix.base) {
            if (const auto* const texture{Skin::Packs::Find(*a_mix.base, a_key)}) return {a_mix.base, texture};
        }

        return {};
    }

    // Adapted from SkinTXST in powerof3's Papyrus Extender
    void SwapMaterial(RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property,
                      RE::BSLightingShaderMaterialBase* a_material,
                      const std::array<const char*, Texture::kTotal>& a_paths) {
        auto* const material{static_cast<RE::BSLightingShaderMaterialBase*>(a_material->Create())};

        if (!material) return;

        auto* const textureSet{RE::BSShaderTextureSet::Create()};

        if (!textureSet) {
            material->~BSLightingShaderMaterialBase();
            RE::free(material);

            return;
        }

        for (std::uint32_t slot = 0; slot < Texture::kTotal; ++slot) {
            if (a_paths[slot]) {
                textureSet->SetTexturePath(static_cast<Texture>(slot), a_paths[slot]);
            }
        }

        material->CopyMembers(a_material);
        material->ClearTextures();
        material->OnLoadTextureSet(0, textureSet);

        if (material->GetFeature() == RE::BSShaderMaterial::Feature::kFaceGen) {
            KeepDirectFaceTextures(static_cast<RE::BSLightingShaderMaterialFacegen*>(material),
                                   static_cast<RE::BSLightingShaderMaterialFacegen*>(a_material), a_paths);
        }

        a_property->SetMaterial(material, true);
        a_property->SetupGeometry(a_geometry);
        a_property->FinishSetupGeometry(a_geometry);

        material->~BSLightingShaderMaterialBase();
        RE::free(material);
    }

    struct Change {
        RE::BSGeometry* geometry{nullptr};
        RE::BSLightingShaderProperty* property{nullptr};
        std::array<std::string, Texture::kTotal> targets;
        std::vector<std::pair<std::string, std::string>> swaps;
    };

    std::optional<Change> Plan(RE::Actor* a_actor, RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property,
                               RE::BSLightingShaderMaterialBase* a_material, const Skin::Mix* a_mix) {
        auto* const oldSet{a_material->textureSet.get()};
        const bool face{a_material->GetFeature() == RE::BSShaderMaterial::Feature::kFaceGen};
        Change change{.geometry = a_geometry, .property = a_property};

        for (const auto slot : swapSlots) {
            const char* const path{oldSet->GetTexturePath(slot)};

            if (!path || !*path) continue;

            const auto key{Skin::Packs::ToPackKey(path)};
            const bool fromPack{Skin::Packs::IsPackPath(path)};
            const std::string original{fromPack ? FindOriginal(a_actor->GetFormID(), a_geometry, key) : path};

            if (!fromPack) {
                RememberOriginal(key, path);
            }

            // Looked up by the game's original texture, so race folders still work after a swap
            const auto lookupKey{original.empty() ? key : Skin::Packs::ToPackKey(original)};
            const auto* const texture{a_mix ? FindTexture(*a_mix, slot, face, lookupKey).texture : nullptr};
            const std::string target{texture ? *texture : fromPack ? original : std::string{}};

            if (target.empty() || stl::cmp(target, path)) continue;

            if (texture && !original.empty()) {
                change.swaps.emplace_back(Skin::Packs::ToPackKey(*texture), original);
            }

            change.targets[slot] = target;
        }

        if (std::ranges::all_of(change.targets, &std::string::empty)) return std::nullopt;

        return change;
    }

    bool Swap(RE::Actor* a_actor, const Change& a_change) {
        // Read again, an earlier swap may have changed it
        auto* const material{static_cast<RE::BSLightingShaderMaterialBase*>(a_change.property->material)};
        auto* const oldSet{material ? material->textureSet.get() : nullptr};

        if (!oldSet) return false;

        std::array<const char*, Texture::kTotal> paths{};

        for (std::uint32_t slot = 0; slot < Texture::kTotal; ++slot) {
            const auto& target{a_change.targets[slot]};
            paths[slot] = target.empty() ? oldSet->GetTexturePath(static_cast<Texture>(slot)) : target.c_str();
        }

        for (const auto& [packKey, original] : a_change.swaps) {
            RememberSwap(a_actor->GetFormID(), a_change.geometry, packKey, original);
        }

        SwapMaterial(a_change.geometry, a_change.property, material, paths);

        return true;
    }
}  // namespace

std::uint32_t Skin::Apply(RE::Actor* a_actor, RE::NiAVObject* a_root, const Mix* a_mix,
                          const std::string_view a_source) {
    if (!a_actor || !a_root) return 0;

    const std::string_view skin{a_mix ? std::string_view{a_mix->name} : "the original skin"};
    std::uint32_t materials{0};
    std::vector<Change> changes;

    RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
        auto* const property{
            netimmerse_cast<RE::BSLightingShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get())};
        auto* const material{property ? static_cast<RE::BSLightingShaderMaterialBase*>(property->material) : nullptr};

        if (!material || !material->textureSet) return RE::BSVisit::BSVisitControl::kContinue;

        ++materials;

        if (auto change{Plan(a_actor, a_geometry, property, material, a_mix)}) {
            changes.push_back(std::move(*change));
        }

        return RE::BSVisit::BSVisitControl::kContinue;
    });

    if (changes.empty()) {
        if (Loader::CanWait()) {
            Loader::Forget(a_actor, a_root);
        }

        return 0;
    }

    // Held until the swap is done, so the game keeps them in memory
    std::vector<RE::NiPointer<RE::NiTexture>> textures;

    if (Loader::CanWait()) {
        std::vector<std::string> paths;

        for (const auto& change : changes) {
            for (const auto& target : change.targets) {
                if (!target.empty() && std::ranges::find(paths, target) == paths.end()) {
                    paths.push_back(target);
                }
            }
        }

        if (!Loader::Request(paths, textures)) {
            Loader::Wait(a_actor, a_root, a_source, skin, std::move(paths), std::move(textures));

            return 0;
        }

        Loader::Forget(a_actor, a_root);
    }

    std::uint32_t swapped{0};

    for (const auto& change : changes) {
        if (Swap(a_actor, change)) {
            ++swapped;
        }
    }

    if (swapped > 0) {
        logger::info("{} {}: swapped {} of {} materials to {}", a_source, Describe(a_actor), swapped, materials, skin);
    }

    return swapped;
}

const Skin::Packs::Pack* Skin::FaceNormalPack(const Mix& a_mix, const std::string_view a_gameNormal) {
    return FindTexture(a_mix, Texture::kNormal, true, Packs::ToPackKey(a_gameNormal)).pack;
}
