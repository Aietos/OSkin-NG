#pragma once
#include "API/OSkinAPI.h"
#include "SKSEMenuFramework.h"
#include "UI/Widgets.h"

namespace UI::PreviewPanel {
    void RenderOverlay(const ImGuiMCP::ImGuiViewport* a_viewport);

    void PlaceWindow(const ImGuiMCP::ImGuiViewport* a_viewport);

    void TrackWindow();

    void OnTargetSelected(bool a_another);

    void Manage(RE::Actor* a_actor);

    void RenderControls(RE::Actor* a_actor);

    bool KeepsMenuOpen();

    [[nodiscard]] ::OSkin::API::MenuState GetShownState(RE::Actor* a_actor);

    void StartNextAs(RE::Actor* a_actor, const ::OSkin::API::MenuState& a_state);
    void ForgetStartAs();

    void SetKeepMenuOpen(bool a_keep);
    bool IsKeepMenuOpenSet();

    void Hover(RE::Actor* a_actor, const std::string& a_skin, const std::optional<std::string>& a_current);

    std::string GetShownSkin(RE::Actor* a_actor);

    bool IsLoading(RE::Actor* a_actor);

    std::vector<Widgets::ControlHint> GetHints(RE::Actor* a_actor);

    void Apply(RE::Actor* a_actor, const std::function<void()>& a_apply);

    void OnMenuClosed();

    bool IsOverlayFocused();

    void End();
}  // namespace UI::PreviewPanel
