#pragma once

namespace Skin::Loader {
    void Pump();

    void Stop();

    [[nodiscard]] bool CanWait();

    [[nodiscard]] bool Request(const std::vector<std::string>& a_paths,
                               std::vector<RE::NiPointer<RE::NiTexture>>& a_textures);

    void Wait(RE::Actor* a_actor, RE::NiAVObject* a_root, std::string_view a_source, std::string_view a_skin,
              std::vector<std::string> a_paths, std::vector<RE::NiPointer<RE::NiTexture>> a_textures);

    void Forget(RE::Actor* a_actor, RE::NiAVObject* a_root);

    [[nodiscard]] bool IsLoading(RE::FormID a_actor);
}  // namespace Skin::Loader
