#include "UI/PreviewPanel.h"
#include "UI/GamepadCamera.h"
#include "UI/UI.h"
#include "UI/Translations.h"
#include "UI/Widgets.h"
#include "Preview/Preview.h"
#include "Skin/Loader.h"

namespace {
    // A preview that couldn't begin isn't attempted again every frame
    RE::FormID g_previewFailedFor{0};

    // The window moves to the side once a preview begins, so the actor can be seen
    bool g_dockWindow{false};
    bool g_docked{false};

    // Where the window was before it moved to the side, it goes back there in the classic UI
    std::optional<std::pair<ImGuiMCP::ImVec2, ImGuiMCP::ImVec2>> g_undockedPlacement;

    bool g_overlayFocused{false};

    float g_controlsWidth{0.0f};

    std::atomic_bool g_keepMenuOpen{false};

    void RenderDragArea(const ImGuiMCP::ImGuiViewport* a_viewport) {
        auto& preview{Preview::Session::GetInstance()};

        ImGuiMCP::SetNextWindowPos(a_viewport->Pos, ImGuiMCP::ImGuiCond_Always);
        ImGuiMCP::SetNextWindowSize(a_viewport->Size, ImGuiMCP::ImGuiCond_Always);

        constexpr auto flags{ImGuiMCP::ImGuiWindowFlags_NoDecoration | ImGuiMCP::ImGuiWindowFlags_NoBackground |
                             ImGuiMCP::ImGuiWindowFlags_NoSavedSettings | ImGuiMCP::ImGuiWindowFlags_NoMove |
                             ImGuiMCP::ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing | ImGuiMCP::ImGuiWindowFlags_NoNav |
                             ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse};

        if (ImGuiMCP::Begin("##OSkinPreviewOverlay", nullptr, flags)) {
            g_overlayFocused = ImGuiMCP::IsWindowFocused();

            ImGuiMCP::SetCursorPos(ImGuiMCP::ImVec2{0.0f, 0.0f});
            ImGuiMCP::InvisibleButton("##OSkinPreviewDrag", a_viewport->Size,
                                      ImGuiMCP::ImGuiButtonFlags_MouseButtonLeft |
                                          ImGuiMCP::ImGuiButtonFlags_MouseButtonRight |
                                          ImGuiMCP::ImGuiButtonFlags_MouseButtonMiddle);

            // Drags are in parts of the screen height, so they feel the same on any screen
            const auto* io{ImGuiMCP::GetIO()};
            if (ImGuiMCP::IsItemActive() && a_viewport->Size.y > 0.0f &&
                (io->MouseDelta.x != 0.0f || io->MouseDelta.y != 0.0f)) {
                const float across{io->MouseDelta.x / a_viewport->Size.y};
                const float down{io->MouseDelta.y / a_viewport->Size.y};
                if (ImGuiMCP::IsMouseDown(0)) {
                    preview.Orbit(across, down);
                } else if (ImGuiMCP::IsMouseDown(1) || ImGuiMCP::IsMouseDown(2)) {
                    preview.Pan(across, down);
                }
            }
            if (ImGuiMCP::IsItemHovered()) {
                if (io->MouseWheel != 0.0f) {
                    preview.Zoom(io->MouseWheel);
                }
                if (ImGuiMCP::IsMouseDoubleClicked(0)) {
                    preview.Recenter();
                }
            }
        }
        ImGuiMCP::End();
    }
}  // namespace

void UI::PreviewPanel::RenderOverlay(const ImGuiMCP::ImGuiViewport* a_viewport) {
    g_overlayFocused = false;
    if (Preview::Session::GetInstance().IsActive()) {
        UI::GamepadCamera::Control();
        RenderDragArea(a_viewport);
    }
}

void UI::PreviewPanel::PlaceWindow(const ImGuiMCP::ImGuiViewport* a_viewport) {
    if (g_dockWindow && Preview::Session::GetInstance().IsActive()) {
        g_dockWindow = false;
        g_docked = true;
        ImGuiMCP::SetNextWindowPos(ImGuiMCP::ImVec2{a_viewport->Pos.x + 16.0f, a_viewport->Pos.y + 16.0f},
                                   ImGuiMCP::ImGuiCond_Always);
        const auto* style{ImGuiMCP::GetStyle()};
        const float content{(std::max)(UI::Widgets::GetTabsFullWidth() * UI::Widgets::GetTextScale(), g_controlsWidth)};
        const float wanted{content + (style->WindowPadding.x * 2.0f) + style->ScrollbarSize};
        const float width{std::clamp(wanted, a_viewport->Size.x * 0.36f, a_viewport->Size.x * 0.45f)};

        ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{width, a_viewport->Size.y - 32.0f}, ImGuiMCP::ImGuiCond_Always);
    } else if (g_docked && !UI::Menu::IsPreviewEnabled()) {
        g_docked = false;
        if (g_undockedPlacement) {
            ImGuiMCP::SetNextWindowPos(g_undockedPlacement->first, ImGuiMCP::ImGuiCond_Always);
            ImGuiMCP::SetNextWindowSize(g_undockedPlacement->second, ImGuiMCP::ImGuiCond_Always);
        }
    }
}

void UI::PreviewPanel::TrackWindow() {
    if (!g_docked) {
        g_undockedPlacement.emplace(ImGuiMCP::GetWindowPos(), ImGuiMCP::GetWindowSize());
    }

    const auto* const viewport{ImGuiMCP::GetMainViewport()};
    const auto pos{ImGuiMCP::GetWindowPos()};
    const float left{pos.x - viewport->Pos.x};
    const float right{left + ImGuiMCP::GetWindowSize().x};
    const float width{viewport->Size.x};

    if (width <= 0.0f) return;

    const float centre{width - right >= left ? (right + width) * 0.5f : left * 0.5f};
    Preview::Session::GetInstance().SetScreenPosition(std::clamp((centre / width * 2.0f) - 1.0f, -0.8f, 0.8f));
}

void UI::PreviewPanel::OnTargetSelected(const bool a_another) {
    g_previewFailedFor = 0;

    // The neutral pose is only for the NPC it was ticked for, so other NPCs' AI and scenes aren't broken
    if (a_another && Preview::Session::IsAlwaysCopy()) {
        Preview::Session::SetAlwaysCopy(false);
    }
}

namespace {
    std::optional<std::pair<RE::FormID, OSkin::API::MenuState>> g_startAs;

    void ApplyStartAs(RE::Actor* a_actor, const OSkin::API::MenuState& a_state) {
        auto& preview{Preview::Session::GetInstance()};

        preview.SetClothed(a_state.clothed);
        preview.SetLit(a_state.lit);

        if (a_state.broughtToPlayer && preview.CanBringToPlayer()) {
            preview.SetBroughtToPlayer(true);
        }

        logger::info("Preview: {} shown as the other menu had them, {}, {}{}", a_actor->GetDisplayFullName(),
                     a_state.clothed ? "dressed" : "undressed", a_state.lit ? "lit" : "not lit",
                     a_state.broughtToPlayer ? ", brought to the player" : "");
    }
}  // namespace

OSkin::API::MenuState UI::PreviewPanel::GetShownState(RE::Actor* a_actor) {
    auto& preview{Preview::Session::GetInstance()};
    OSkin::API::MenuState state;

    if (a_actor && preview.IsPreviewing(a_actor)) {
        state.broughtToPlayer = preview.IsBroughtToPlayer();
        state.clothed = preview.IsClothed();
        state.lit = preview.IsLit();
    }

    return state;
}

void UI::PreviewPanel::StartNextAs(RE::Actor* a_actor, const OSkin::API::MenuState& a_state) {
    if (a_actor) {
        g_startAs = std::pair{a_actor->GetFormID(), a_state};
    }
}

void UI::PreviewPanel::ForgetStartAs() { g_startAs.reset(); }

void UI::PreviewPanel::Manage(RE::Actor* a_actor) {
    auto& preview{Preview::Session::GetInstance()};

    // Only for a preview that begins on the actor now. Otherwise it would come back on a later one
    const auto startAs{std::exchange(g_startAs, std::nullopt)};

    if (!UI::Menu::IsPreviewEnabled() || !a_actor->Is3DLoaded()) {
        End();
        return;
    }

    if (!preview.IsPreviewing(a_actor) && g_previewFailedFor != a_actor->GetFormID()) {
        if (preview.Begin(a_actor)) {
            g_dockWindow = true;

            if (startAs && startAs->first == a_actor->GetFormID()) {
                ApplyStartAs(a_actor, startAs->second);
            }
        } else {
            g_previewFailedFor = a_actor->GetFormID();
        }
    }

    preview.Update();
}

void UI::PreviewPanel::RenderControls(RE::Actor* a_actor) {
    auto& preview{Preview::Session::GetInstance()};
    const bool previewing{preview.IsPreviewing(a_actor)};
    const std::array labels{UI::Translations::Get("oskin_live_preview"), UI::Translations::Get("oskin_keep_menu_open"),
                            UI::Translations::Get("oskin_neutral_pose"), UI::Translations::Get("oskin_show_clothes"),
                            UI::Translations::Get("oskin_light"),        UI::Translations::Get("oskin_bring_to_me")};

    float widest{0.0f};

    for (const auto& label : labels) {
        widest = (std::max)(widest, UI::Widgets::CheckboxWidth(label.c_str()));
    }

    g_controlsWidth = 3.0f * (widest + (ImGuiMCP::GetStyle()->CellPadding.x * 2.0f));

    constexpr auto tableFlags{ImGuiMCP::ImGuiTableFlags_SizingStretchSame};

    if (ImGuiMCP::BeginTable("##PreviewOptions", 3, tableFlags)) {
        ImGuiMCP::TableNextColumn();

        if (bool enabled{UI::Menu::IsPreviewEnabled()}; UI::Widgets::Checkbox(labels[0].c_str(), &enabled)) {
            UI::Menu::SetPreviewEnabled(enabled);
        }

        if (UI::Menu::IsPreviewEnabled()) {
            ImGuiMCP::TableNextColumn();

            if (bool keep{g_keepMenuOpen.load()}; UI::Widgets::Checkbox(labels[1].c_str(), &keep)) {
                g_keepMenuOpen.store(keep);
            }

            ImGuiMCP::TableNextColumn();

            if (bool always{Preview::Session::IsAlwaysCopy()}; UI::Widgets::Checkbox(labels[2].c_str(), &always)) {
                Preview::Session::SetAlwaysCopy(always);
            }
        }

        if (previewing) {
            ImGuiMCP::TableNextColumn();

            if (bool clothed{preview.IsClothed()}; UI::Widgets::Checkbox(labels[3].c_str(), &clothed)) {
                preview.SetClothed(clothed);
            }

            ImGuiMCP::TableNextColumn();

            if (bool lit{preview.IsLit()}; UI::Widgets::Checkbox(labels[4].c_str(), &lit)) {
                preview.SetLit(lit);
            }

            // Greyed out when it can't be done, so the controls keep their places
            ImGuiMCP::TableNextColumn();

            bool brought{preview.IsBroughtToPlayer()};

            ImGuiMCP::BeginDisabled(!brought && !preview.CanBringToPlayer());

            if (UI::Widgets::Checkbox(labels[5].c_str(), &brought)) {
                preview.SetBroughtToPlayer(brought);
            }
            ImGuiMCP::EndDisabled();
        }

        ImGuiMCP::EndTable();
    }

    if (previewing) {
        ImGuiMCP::Dummy(ImGuiMCP::ImVec2{0.0f, ImGuiMCP::GetTextLineHeight() * 0.3f});

        if (ImGuiMCP::BeginTable("##PreviewButtons", 3, tableFlags)) {
            constexpr ImGuiMCP::ImVec2 fullWidth{-FLT_MIN, 0.0f};

            ImGuiMCP::TableNextColumn();

            if (ImGuiMCP::Button(UI::Translations::Get("oskin_reset_view").c_str(), fullWidth)) {
                preview.ResetView();
            }

            ImGuiMCP::TableNextColumn();

            if (ImGuiMCP::Button(UI::Translations::Get("oskin_face_camera").c_str(), fullWidth)) {
                preview.FaceCamera();
            }

            ImGuiMCP::TableNextColumn();

            if (ImGuiMCP::Button(UI::Translations::Get("oskin_recenter").c_str(), fullWidth)) {
                preview.Recenter();
            }

            ImGuiMCP::EndTable();
        }
    } else if (!a_actor->Is3DLoaded()) {
        const ImGuiMCP::ImVec2 gap{0.0f, ImGuiMCP::GetTextLineHeight() * 0.4f};

        ImGuiMCP::Dummy(gap);
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4{0.7f, 0.7f, 0.75f, 1.0f});
        ImGuiMCP::TextWrapped("%s", UI::Translations::Get("oskin_preview_unloaded").c_str());
        ImGuiMCP::PopStyleColor();
        ImGuiMCP::Dummy(gap);
    }
}

bool UI::PreviewPanel::KeepsMenuOpen() { return g_keepMenuOpen.load() && UI::Menu::IsPreviewEnabled(); }

void UI::PreviewPanel::SetKeepMenuOpen(const bool a_keep) { g_keepMenuOpen.store(a_keep); }

bool UI::PreviewPanel::IsKeepMenuOpenSet() { return g_keepMenuOpen.load(); }

void UI::PreviewPanel::Hover(RE::Actor* a_actor, const std::string& a_skin,
                             const std::optional<std::string>& a_current) {
    if (auto& preview{Preview::Session::GetInstance()}; preview.IsPreviewing(a_actor)) {
        preview.RequestSkin(a_skin == a_current ? std::string{} : a_skin);
    }
}

std::vector<UI::Widgets::ControlHint> UI::PreviewPanel::GetHints(RE::Actor* a_actor) {
    if (!Preview::Session::GetInstance().IsPreviewing(a_actor)) {
        return {};
    }

    if (UI::GamepadCamera::IsInUse()) {
        return UI::GamepadCamera::Hints();
    }

    const auto get{[](const char* a_key) { return UI::Translations::Get(a_key); }};

    return {{{get("oskin_hint_left_drag")}, "", get("oskin_hint_orbit")},
            {{get("oskin_hint_right_drag")}, "", get("oskin_hint_pan")},
            {{get("oskin_hint_wheel")}, "", get("oskin_hint_zoom")},
            {{get("oskin_hint_double_click")}, "", get("oskin_recenter")}};
}

std::string UI::PreviewPanel::GetShownSkin(RE::Actor* a_actor) {
    auto& preview{Preview::Session::GetInstance()};

    return preview.IsPreviewing(a_actor) ? preview.GetShownSkin() : std::string{};
}

bool UI::PreviewPanel::IsLoading(RE::Actor* a_actor) {
    if (Skin::Loader::IsLoading(a_actor->GetFormID())) return true;

    auto& preview{Preview::Session::GetInstance()};
    const auto shown{preview.IsPreviewing(a_actor) ? preview.GetShownActorID() : 0};

    return shown != 0 && Skin::Loader::IsLoading(shown);
}

void UI::PreviewPanel::Apply(RE::Actor* a_actor, const std::function<void()>& a_apply) {
    auto& preview{Preview::Session::GetInstance()};

    if (!preview.IsPreviewing(a_actor)) {
        a_apply();
    } else if (KeepsMenuOpen()) {
        preview.Apply(a_apply);
    } else {
        preview.End(true);
        a_apply();
    }
}

void UI::PreviewPanel::OnMenuClosed() { Preview::Session::GetInstance().OnMenuClosed(); }

bool UI::PreviewPanel::IsOverlayFocused() { return g_overlayFocused && Preview::Session::GetInstance().IsActive(); }

void UI::PreviewPanel::End() {
    if (auto& preview{Preview::Session::GetInstance()}; preview.IsActive()) {
        preview.End();
    }
}
