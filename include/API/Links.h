#pragma once

#include "API/OSkinAPI.h"

namespace API {
    namespace OBody {
        void Request();

        [[nodiscard]] bool IsAvailable();

        bool OpenPresetMenu(RE::Actor* a_actor, const ::OSkin::API::MenuState& a_state);

        [[nodiscard]] RE::Actor* GetPreviewCopyTarget(RE::Actor* a_actor);

        [[nodiscard]] std::string GetPreset(RE::Actor* a_actor);

        [[nodiscard]] std::vector<std::string> GetPresetNames(bool a_female);
    }  // namespace OBody

    namespace RaceMenu {
        void Request();

        void CopyBody(RE::Actor* a_from, RE::Actor* a_to);
    }  // namespace RaceMenu

    namespace OSkin {
        void Register();
    }  // namespace OSkin
}  // namespace API
