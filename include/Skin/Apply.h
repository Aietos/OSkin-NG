#pragma once

#include "Skin/Packs.h"

namespace Skin {
    enum Field : std::size_t { kDiffuse, kNormal, kSubsurface, kSpecular, kHead, kHeadNormal, kTeeth, kFieldTotal };

    inline constexpr std::array<std::string_view, kFieldTotal> FieldNames{
        "diffuse", "normal", "subsurface", "specular", "head", "headNormal", "teeth"};

    struct Mix {
        std::string name;
        const Packs::Pack* base{nullptr};
        const Packs::Pack* diffuse{nullptr};
        const Packs::Pack* normal{nullptr};
        const Packs::Pack* subsurface{nullptr};
        const Packs::Pack* specular{nullptr};
        const Packs::Pack* head{nullptr};
        const Packs::Pack* headNormal{nullptr};
        const Packs::Pack* teeth{nullptr};
        std::array<bool, kFieldTotal> gameParts{};

        [[nodiscard]] const Packs::Pack*& Get(const Field a_field) {
            return const_cast<const Packs::Pack*&>(std::as_const(*this).Get(a_field));
        }

        [[nodiscard]] const Packs::Pack* const& Get(const Field a_field) const {
            switch (a_field) {
                case kDiffuse:
                    return diffuse;
                case kNormal:
                    return normal;
                case kSubsurface:
                    return subsurface;
                case kSpecular:
                    return specular;
                case kHead:
                    return head;
                case kHeadNormal:
                    return headNormal;
                default:
                    return teeth;
            }
        }
    };

    std::uint32_t Apply(RE::Actor* a_actor, RE::NiAVObject* a_root, const Mix* a_mix, std::string_view a_source);

    const Packs::Pack* FaceNormalPack(const Mix& a_mix, std::string_view a_gameNormal);
}  // namespace Skin
