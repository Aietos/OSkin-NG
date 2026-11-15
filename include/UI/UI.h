#pragma once
#include "SKSEMenuFramework.h"
#include "UI/PreviewPanel.h"

namespace UI {
    void Register();

    namespace Menu {
        void __stdcall Render();
        inline MENU_WINDOW Window;

        void SetHotkeyScanCode(std::uint32_t scanCode);

        std::uint32_t GetHotkeyScanCode();

        void SetPreviewEnabled(bool a_enabled);
        bool IsPreviewEnabled();

        void ShowConfigEditor();

        bool OpenFor(RE::Actor* a_actor, const ::OSkin::API::MenuState* a_state);

        void OpenInOBody(RE::Actor* a_actor);

        void BeginHotkeyCapture();
        void CancelHotkeyCapture();
        bool IsCapturingHotkey();
        std::optional<std::uint32_t> TakeCapturedHotkey();

        std::string GetKeyName(std::uint32_t a_scanCode);
    }  // namespace Menu
};  // namespace UI
