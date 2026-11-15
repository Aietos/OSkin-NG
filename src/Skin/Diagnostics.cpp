#include "Skin/Diagnostics.h"

#include "STL.h"

namespace {
    using Texture = RE::BSTextureSet::Texture;

    std::string TextureName(const RE::NiPointer<RE::NiSourceTexture>& a_texture) {
        if (!a_texture) return "none";

        const char* const name{a_texture->name.c_str()};

        return std::format("{}{}", name ? name : "", a_texture->rendererTexture ? "" : " (NOT LOADED)");
    }

    void LogActor(RE::Actor* a_actor, const std::string_view a_label) {
        auto* const root{a_actor ? a_actor->Get3D(false) : nullptr};

        if (!root) return;

        const char* const name{a_actor->GetName()};

        logger::info("Skin textures of {} {} ({:08X})", a_label, name ? name : "", a_actor->GetFormID());

        RE::BSVisit::TraverseScenegraphGeometries(root, [](RE::BSGeometry* a_geometry) {
            auto* const property{netimmerse_cast<RE::BSLightingShaderProperty*>(
                a_geometry->GetGeometryRuntimeData().shaderProperty.get())};
            auto* const material{property ? static_cast<RE::BSLightingShaderMaterialBase*>(property->material)
                                          : nullptr};
            auto* const textureSet{material ? material->textureSet.get() : nullptr};

            if (!textureSet) return RE::BSVisit::BSVisitControl::kContinue;

            const auto feature{material->GetFeature()};
            const char* const diffuse{textureSet->GetTexturePath(Texture::kDiffuse)};
            const std::string_view geometryName{a_geometry->name.c_str()};
            const std::string_view diffusePath{diffuse ? diffuse : ""};

            // The mouth isn't a face material, so it's found by name
            const bool mouth{stl::contains(geometryName, "teeth") || stl::contains(geometryName, "mouth") ||
                             stl::contains(geometryName, "tongue") || stl::contains(diffusePath, "mouth") ||
                             stl::contains(diffusePath, "teeth")};

            if (!mouth && feature != RE::BSShaderMaterial::Feature::kFaceGen &&
                feature != RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            std::string paths;

            for (const auto slot : {Texture::kDiffuse, Texture::kNormal, Texture::kSubsurfaceTint, Texture::kDetailMap,
                                    Texture::kMultilayer, Texture::kSpecular}) {
                const char* const path{textureSet->GetTexturePath(slot)};

                if (path && *path) {
                    paths += std::format(" [{}] {}", std::to_underlying(slot), path);
                }
            }

            logger::info("    {} {:X} (shader {}){}", a_geometry->name.c_str(),
                         reinterpret_cast<std::uintptr_t>(a_geometry), std::to_underlying(feature), paths);
            logger::info("        loaded {}", Skin::Diagnostics::LoadedTextures(material));

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    class ConsoleSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (!a_event || !a_event->opening || a_event->menuName != RE::Console::MENU_NAME) {
                return RE::BSEventNotifyControl::kContinue;
            }

            LogActor(RE::PlayerCharacter::GetSingleton(), "the player");

            const auto selected{RE::Console::GetSelectedRef()};

            if (auto* const actor{selected ? selected->As<RE::Actor>() : nullptr}; actor && !actor->IsPlayerRef()) {
                LogActor(actor, "the console selection");
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}  // namespace

std::string Skin::Diagnostics::LoadedTextures(RE::BSLightingShaderMaterialBase* a_material) {
    if (!a_material) return {};

    auto text{std::format("diffuse {}, normal {}, specular {}", TextureName(a_material->diffuseTexture),
                          TextureName(a_material->normalTexture),
                          TextureName(a_material->specularBackLightingTexture))};

    if (a_material->GetFeature() == RE::BSShaderMaterial::Feature::kFaceGen) {
        const auto* const faceGen{static_cast<RE::BSLightingShaderMaterialFacegen*>(a_material)};
        text += std::format(", subsurface {}, detail {}, tint {}", TextureName(faceGen->subsurfaceTexture),
                            TextureName(faceGen->detailTexture), TextureName(faceGen->tintTexture));
    }

    return text;
}

void Skin::Diagnostics::Register() {
    static ConsoleSink sink;

    if (auto* const ui{RE::UI::GetSingleton()}) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
        logger::info("Opening the console logs the skin textures of the player and the selected actor");
    }
}
