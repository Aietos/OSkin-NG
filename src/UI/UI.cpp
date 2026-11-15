#include "UI/UI.h"
#include "UI/Translations.h"
#include "UI/ConfigEditor.h"
#include "UI/ConfigTabs.h"
#include "UI/GamepadCamera.h"
#include "UI/NpcList.h"
#include "UI/PacksTab.h"
#include "UI/SkinsTab.h"
#include "UI/PreviewPanel.h"
#include "UI/Util.h"
#include "UI/Widgets.h"
#include "Config/Config.h"
#include "STL.h"
#include "Skin/Loader.h"
#include "API/Links.h"

namespace {
    std::atomic<std::uint32_t> g_hotkeyScanCode{0x25};
    std::atomic_bool g_previewEnabled{true};

    std::atomic_bool g_capturingHotkey{false};
    std::atomic<std::uint32_t> g_capturedHotkey{0};

    // While the menu waits for a key, the game is in text entry, so its controls and mods like OBody ignore the key
    // Main thread only
    bool g_holdingTextEntry{false};

    void HoldTextEntry(const bool a_hold) {
        if (g_holdingTextEntry == a_hold || !stl::IsMainThread()) return;

        if (auto* const controls{RE::ControlMap::GetSingleton()}) {
            controls->AllowTextInput(a_hold);
            g_holdingTextEntry = a_hold;
        }
    }

    bool g_closeAfterTextInput{false};

    constexpr auto kNumKeyboardKeys{static_cast<std::uint32_t>(SKSE::InputMap::kMacro_NumKeyboardKeys)};
    constexpr auto kNumMouseButtons{static_cast<std::uint32_t>(SKSE::InputMap::kMacro_NumMouseButtons)};
    constexpr auto kMouseButtonOffset{static_cast<std::uint32_t>(SKSE::InputMap::kMacro_MouseButtonOffset)};
    constexpr auto kInvalidKeyCode{static_cast<std::uint32_t>(SKSE::InputMap::kMaxMacros)};

    std::uint32_t ToMacroKeyCode(const RE::ButtonEvent& a_button) {
        const auto id = a_button.GetIDCode();
        switch (a_button.GetDevice()) {
            case RE::INPUT_DEVICE::kKeyboard:
                return id < kNumKeyboardKeys ? id : kInvalidKeyCode;
            case RE::INPUT_DEVICE::kMouse:
                return id < kNumMouseButtons ? kMouseButtonOffset + id : kInvalidKeyCode;
            case RE::INPUT_DEVICE::kGamepad:
                return SKSE::InputMap::GamepadMaskToKeycode(id);
            default:
                return kInvalidKeyCode;
        }
    }

    // The idle camera spins around the player after a while without input, which happens while the menu is open
    // The camera keeps its own copy of bDisableAutoVanityMode, so it's turned off here while the menu is open
    std::optional<bool> g_autoVanityAllowedBefore;

    void DisableAutoVanity() {
        auto* const camera{RE::PlayerCamera::GetSingleton()};

        if (!camera) return;

        auto& allowed{camera->GetRuntimeData2().allowAutoVanityMode};

        if (!g_autoVanityAllowedBefore) {
            g_autoVanityAllowedBefore = allowed;
        }

        allowed = false;
    }

    void RestoreAutoVanity() {
        if (!g_autoVanityAllowedBefore) return;

        if (auto* const camera{RE::PlayerCamera::GetSingleton()}) {
            camera->GetRuntimeData2().allowAutoVanityMode = *g_autoVanityAllowedBefore;
        }

        g_autoVanityAllowedBefore.reset();
    }

    bool IsGameTextEntryActive() {
        const auto* controlMap = RE::ControlMap::GetSingleton();
        return controlMap && controlMap->GetRuntimeData().textEntryCount > 0;
    }

    constexpr std::uint32_t kVRTriggerButton{33};

    bool IsVRController(const RE::INPUT_DEVICE a_device) {
        if (!REL::Module::IsVR()) return false;

        switch (a_device) {
            case RE::INPUT_DEVICE::kVivePrimary:
            case RE::INPUT_DEVICE::kViveSecondary:
            case RE::INPUT_DEVICE::kOculusPrimary:
            case RE::INPUT_DEVICE::kOculusSecondary:
            case RE::INPUT_DEVICE::kWMRPrimary:
            case RE::INPUT_DEVICE::kWMRSecondary:
                return true;
            default:
                return false;
        }
    }

    bool g_vrTriggerDown{false};

    // SKSE Menu Framework doesn't take the VR trigger as a click, so it's turned into a left click
    void ClickWithVRTrigger(const RE::ButtonEvent& a_button) {
        if (!IsVRController(a_button.GetDevice()) || a_button.GetIDCode() != kVRTriggerButton) return;

        const bool open{UI::Menu::Window && UI::Menu::Window->IsOpen.load()};

        if (a_button.IsDown() && open && !g_vrTriggerDown) {
            static bool logged{false};

            if (!logged) {
                logger::info("VR Trigger click detected, translating to left mouse button click");
                logged = true;
            }

            ImGuiMCP::ImGuiIOManager::AddMouseButtonEvent(ImGuiMCP::GetIO(), 0, true);
            g_vrTriggerDown = true;
        } else if (!a_button.IsPressed() && g_vrTriggerDown) {
            ImGuiMCP::ImGuiIOManager::AddMouseButtonEvent(ImGuiMCP::GetIO(), 0, false);
            g_vrTriggerDown = false;
        }
    }

    bool __stdcall OnHotkeyInput(RE::InputEvent* a_event) {
        if (!a_event) {
            return false;
        }

        if (const auto* const button{a_event->AsButtonEvent()}) {
            ClickWithVRTrigger(*button);
        }

        if (const auto* const button{a_event->AsButtonEvent()}; button && g_capturingHotkey.load() &&
                                                                button->IsDown() &&
                                                                button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
            g_capturedHotkey = button->GetIDCode();
            g_capturingHotkey = false;

            return false;
        }

        if (g_holdingTextEntry && !g_capturingHotkey.load() && UI::Menu::Window && !UI::Menu::Window->IsOpen.load()) {
            HoldTextEntry(false);
        }

        const auto scanCode = g_hotkeyScanCode.load();

        if (scanCode == 0) {
            return false;
        }

        if (auto* button = a_event->AsButtonEvent()) {
            if (button->IsDown() && ToMacroKeyCode(*button) == scanCode) {
                if (!UI::Menu::Window) {
                    return false;
                }

                if (IsGameTextEntryActive()) {
                    return false;
                }

                const bool isCurrentlyOpen = UI::Menu::Window->IsOpen.load();

                if (!isCurrentlyOpen) {
                    auto* ui = RE::UI::GetSingleton();
                    if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) {
                        return false;
                    }
                }

                UI::Menu::Window->IsOpen = !isCurrentlyOpen;
            }
        }
        return false;
    }

    long long NowMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::atomic<long long> g_frameworkOpenedAt{0};
    std::atomic_bool g_openedFromSection{false};

    void ForgetHandOff();

    void __stdcall OnMenuFrameworkEvent(const SKSEMenuFramework::Model::EventType a_type) {
        // The window can be closed without rendering again, by the hotkey or by SKSE Menu Framework itself
        if (UI::Menu::Window && !UI::Menu::Window->IsOpen.load()) {
            UI::PreviewPanel::End();
            RestoreAutoVanity();
            Skin::Loader::Stop();
            ForgetHandOff();
        }

        if (a_type == SKSEMenuFramework::Model::EventType::kOpenMenu) {
            g_frameworkOpenedAt = NowMillis();
            g_openedFromSection = false;
        }

        if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
            UI::PreviewPanel::OnMenuClosed();
        }
    }

    class CrosshairSink final : public RE::BSTEventSink<SKSE::CrosshairRefEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event,
                                              RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override {
            std::unique_lock lock{mutex_};

            actor_ = a_event && a_event->crosshairRef ? a_event->crosshairRef->As<RE::Actor>() : nullptr;

            return RE::BSEventNotifyControl::kContinue;
        }

        RE::NiPointer<RE::Actor> Get() {
            std::shared_lock lock{mutex_};

            return RE::NiPointer<RE::Actor>{actor_};
        }

    private:
        std::shared_mutex mutex_;
        RE::Actor* actor_{nullptr};
    };

    CrosshairSink g_crosshair;

    RE::NiPointer<RE::Actor> GetCrosshairActor() { return g_crosshair.Get(); }

    void OpenFromFramework() {
        UI::Menu::Window->IsOpen = true;

        if (auto* const framework{SKSEMenuFramework::GetMainWindow()}) {
            framework->IsOpen = false;
        }
    }

    long long g_lastSectionRender{0};

    void __stdcall RenderSectionItem() {
        if (!UI::Menu::Window) return;

        const auto now{NowMillis()};
        const bool justPicked{now - g_lastSectionRender > 200};

        g_lastSectionRender = now;

        // Picking OSkin in the framework's list opens it at once, but not when the framework opens with OSkin picked
        // already, or it would close right away. Then the button does it
        if (justPicked && now - g_frameworkOpenedAt.load() > 300 && !g_openedFromSection.exchange(true)) {
            OpenFromFramework();

            return;
        }

        if (ImGuiMCP::Button(UI::Translations::Get("oskin_open_menu").c_str())) {
            OpenFromFramework();
        }
    }

    RE::ActorHandle g_openFor;

    RE::ActorHandle g_switchTo;

    // The mouse cursor where it was in the other mod's menu, put back for the first frames since SKSE Menu Framework
    // may center it as the window opens
    struct CursorRestore {
        float x{0.0f};
        float y{0.0f};
        int frames{0};
    };

    std::optional<CursorRestore> g_cursorRestore;

    // What another mod's menu handed over is only for the time this menu stays open
    void ForgetHandOff() {
        g_openFor = {};
        g_cursorRestore.reset();
        UI::PreviewPanel::ForgetStartAs();
    }

    // The game's window when it's in front, so the cursor is never moved over another app
    HWND GetGameWindowInFront() {
        auto* const window{GetForegroundWindow()};
        DWORD process{0};

        if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) return nullptr;

        return window;
    }

    void RestoreCursor() {
        if (!g_cursorRestore) return;

        auto& restore{*g_cursorRestore};

        ImGuiMCP::ImGuiIOManager::AddMousePosEvent(ImGuiMCP::GetIO(), restore.x, restore.y);

        if (auto* const window{GetGameWindowInFront()}) {
            POINT point{std::lround(restore.x), std::lround(restore.y)};

            if (ClientToScreen(window, &point)) {
                SetCursorPos(point.x, point.y);
            }
        }

        if (--restore.frames <= 0) {
            g_cursorRestore.reset();
        }
    }

    bool g_editorTabShown{false};

    // The NPC in the crosshair or the player, picked when the menu opens. Kept while it's open, since the preview's
    // camera changes what the crosshair is on
    RE::ActorHandle g_target;
    bool g_selectSkinsTab{false};
    bool g_selectNpcTab{false};
    bool g_selectEditorTab{false};

    void SelectTarget(RE::Actor* a_actor, const bool a_menuOpened = false) {
        const bool another{a_menuOpened || g_target.get().get() != a_actor};
        g_target = a_actor->GetHandle();
        UI::PreviewPanel::OnTargetSelected(another);

        if (another) {
            UI::SkinsTab::OnTargetSelected();
        }
    }
}  // namespace

void UI::Menu::SetHotkeyScanCode(std::uint32_t scanCode) { g_hotkeyScanCode.store(scanCode == 0x01 ? 0 : scanCode); }

std::uint32_t UI::Menu::GetHotkeyScanCode() { return g_hotkeyScanCode.load(); }

void UI::Menu::SetPreviewEnabled(const bool a_enabled) { g_previewEnabled.store(a_enabled); }

bool UI::Menu::IsPreviewEnabled() { return g_previewEnabled.load(); }

void UI::Menu::ShowConfigEditor() { g_selectEditorTab = true; }

void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        RE::DebugMessageBox(UI::Translations::Get("oskin_skse_menu_framework_failure").c_str());
    }

    Menu::Window = SKSEMenuFramework::AddWindow(Menu::Render);

    // Also opens from SKSE Menu Framework's own menu, for when there's no key
    SKSEMenuFramework::SetSection("OSkin");
    SKSEMenuFramework::AddSectionItem(UI::Translations::Get("oskin_section_item"), RenderSectionItem);

    static auto* inputHandle = SKSEMenuFramework::AddInputEvent(OnHotkeyInput);
    static auto* menuEventHandle = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);

    if (auto* const crosshairEvents{SKSE::GetCrosshairRefEventSource()}) {
        crosshairEvents->AddEventSink<SKSE::CrosshairRefEvent>(&g_crosshair);
    }

    Menu::SetHotkeyScanCode(Parser::JSONParser::GetInstance().GetConfig()->GetMenuHotkey());
}

void __stdcall UI::Menu::Render() {
    bool isOpen = Window->IsOpen.load();

    if (!isOpen) {
        PreviewPanel::End();
        RestoreAutoVanity();
        Skin::Loader::Stop();
        CancelHotkeyCapture();
        ForgetHandOff();
        return;
    }

    DisableAutoVanity();
    Skin::Loader::Pump();

    auto* viewport = ImGuiMCP::GetMainViewport();

    PreviewPanel::RenderOverlay(viewport);

    const auto center = ImGuiMCP::ImGuiViewportManager::GetCenter(viewport);

    ImGuiMCP::SetNextWindowPos(center, ImGuiMCP::ImGuiCond_FirstUseEver, ImGuiMCP::ImVec2{0.5f, 0.5f});
    ImGuiMCP::SetNextWindowSize(ImGuiMCP::ImVec2{viewport->Size.x * 0.38f, viewport->Size.y * 0.75f},
                                ImGuiMCP::ImGuiCond_FirstUseEver);

    PreviewPanel::PlaceWindow(viewport);

    // The Config Editor's height changes with its rules, so its scrollbar is always shown to keep the width steady
    const ImGuiMCP::ImGuiWindowFlags windowFlags{g_editorTabShown ? ImGuiMCP::ImGuiWindowFlags_AlwaysVerticalScrollbar
                                                                  : ImGuiMCP::ImGuiWindowFlags_None};

    if (!ImGuiMCP::Begin("OSkin##OSkinWindow", &isOpen, windowFlags)) {
        ImGuiMCP::End();
        Window->IsOpen = isOpen;

        return;
    }

    PreviewPanel::TrackWindow();

    const std::array<std::string, 7> tabs{
        UI::Translations::Get("oskin_skins_tab"),        UI::Translations::Get("oskin_npcs_tab"),
        UI::Translations::Get("oskin_packs_tab"),        UI::Translations::Get("oskin_settings_header"),
        UI::Translations::Get("oskin_editor_tab"),       UI::Translations::Get("oskin_include_files_header"),
        UI::Translations::Get("oskin_config_viewer_tab")};
    Widgets::UpdateTextScale(tabs);
    Widgets::PushScaledSpacing();

    const bool justOpened = ImGuiMCP::IsWindowAppearing();
    RestoreCursor();

    const auto openForHandle{std::exchange(g_openFor, RE::ActorHandle{}).get()};
    RE::Actor* const openFor{openForHandle && Util::IsSkinTarget(openForHandle.get()) ? openForHandle.get() : nullptr};

    if (justOpened) {
        g_closeAfterTextInput = false;
        ImGuiMCP::SetScrollY(0.0f);
        SkinsTab::Reset();
        ConfigTabs::Reset();
        ConfigEditor::Reset();
        PacksTab::Reset();

        const auto crosshairTarget{GetCrosshairActor()};
        RE::Actor* crosshairActor{crosshairTarget.get()};
        if (openFor) {
            SelectTarget(openFor, true);
        } else if (Util::IsSkinTarget(crosshairActor)) {
            SelectTarget(crosshairActor, true);
        } else {
            SelectTarget(RE::PlayerCharacter::GetSingleton(), true);
        }

        GamepadCamera::OnMenuOpened();
    } else if (openFor) {
        SelectTarget(openFor);
    }

    if (openFor) {
        g_selectSkinsTab = true;
    }

    if (g_closeAfterTextInput) {
        g_closeAfterTextInput = false;
        isOpen = false;
    }

    if ((ImGuiMCP::IsWindowFocused(ImGuiMCP::ImGuiFocusedFlags_RootAndChildWindows) ||
         PreviewPanel::IsOverlayFocused()) &&
        ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_Escape, false)) {
        if (ImGuiMCP::IsPopupOpen("", ImGuiMCP::ImGuiPopupFlags_AnyPopup) || Widgets::WasComboOpen()) {
            ImGuiMCP::ClearActiveID();
            Widgets::CloseOpenCombo();
        } else if (ImGuiMCP::IsAnyItemActive()) {
            ImGuiMCP::ClearActiveID();
            g_closeAfterTextInput = true;
        } else {
            isOpen = false;
        }
    }

    // The NPC picked may have been unloaded or deleted since
    RE::NiPointer<RE::Actor> target{g_target.get()};

    if (!target) {
        SelectTarget(RE::PlayerCharacter::GetSingleton());
        target = g_target.get();
    }
    RE::Actor* actor{target.get()};

    PreviewPanel::Manage(actor);

    if (ImGuiMCP::BeginTabBar("##OSkinTabs", ImGuiMCP::ImGuiTabBarFlags_FittingPolicyScroll)) {
        const bool selectSkinsTab{justOpened || g_selectSkinsTab};
        g_selectSkinsTab = false;

        if (ImGuiMCP::BeginTabItem(
                tabs[0].c_str(), nullptr,
                selectSkinsTab ? ImGuiMCP::ImGuiTabItemFlags_SetSelected : ImGuiMCP::ImGuiTabItemFlags_None)) {
            if (SkinsTab::Render(actor)) {
                // An NPC that isn't loaded can't be seen, the menu goes back to the NPCs to pick the next one
                if (!actor->Is3DLoaded()) {
                    g_selectNpcTab = true;
                } else if (!PreviewPanel::KeepsMenuOpen()) {
                    isOpen = false;
                }
            }
            ImGuiMCP::EndTabItem();
        }

        const bool selectNpcTab{g_selectNpcTab};
        g_selectNpcTab = false;

        static bool npcTabWasOpen{false};

        if (ImGuiMCP::BeginTabItem(
                tabs[1].c_str(), nullptr,
                selectNpcTab ? ImGuiMCP::ImGuiTabItemFlags_SetSelected : ImGuiMCP::ImGuiTabItemFlags_None)) {
            if (auto* const picked{NpcList::Render(actor, !npcTabWasOpen || justOpened)}) {
                SelectTarget(picked);
                g_selectSkinsTab = true;
            }
            npcTabWasOpen = true;
            ImGuiMCP::EndTabItem();
        } else {
            npcTabWasOpen = false;
        }

        if (ImGuiMCP::BeginTabItem(tabs[2].c_str())) {
            PacksTab::Render();
            ImGuiMCP::EndTabItem();
        }

        if (ImGuiMCP::BeginTabItem(tabs[3].c_str())) {
            ConfigTabs::RenderSettings();
            ImGuiMCP::EndTabItem();
        }

        const bool selectEditorTab{g_selectEditorTab};
        g_selectEditorTab = false;

        g_editorTabShown = ImGuiMCP::BeginTabItem(
            tabs[4].c_str(), nullptr,
            selectEditorTab ? ImGuiMCP::ImGuiTabItemFlags_SetSelected : ImGuiMCP::ImGuiTabItemFlags_None);

        if (g_editorTabShown) {
            ConfigEditor::Render(actor);
            ImGuiMCP::EndTabItem();
        }

        if (ImGuiMCP::BeginTabItem(tabs[5].c_str())) {
            ConfigTabs::RenderIncludeFiles();
            ImGuiMCP::EndTabItem();
        }

        if (ImGuiMCP::BeginTabItem(tabs[6].c_str())) {
            ConfigTabs::RenderViewer();
            ImGuiMCP::EndTabItem();
        }

        ImGuiMCP::EndTabBar();
    }

    Widgets::PopScaledSpacing();
    ImGuiMCP::End();

    // This menu closes and its preview ends this frame, so OBody's preview starts from the game's camera and clothes
    // It stays open if OBody's can't open
    if (const auto switchTo{std::exchange(g_switchTo, RE::ActorHandle{}).get()}) {
        // Taken before the preview ends, OBody's preview starts the same way
        auto handOff{PreviewPanel::GetShownState(switchTo.get())};
        const auto mouse{ImGuiMCP::GetMousePos()};
        handOff.cursorX = mouse.x;
        handOff.cursorY = mouse.y;

        if (API::OBody::OpenPresetMenu(switchTo.get(), handOff)) {
            isOpen = false;
        } else {
            logger::warn("OBody's menu didn't open on {}", switchTo->GetDisplayFullName());
        }
    }

    Window->IsOpen = isOpen;
    Widgets::EndFrame();

    if (!isOpen) {
        PreviewPanel::End();
        RestoreAutoVanity();
        Skin::Loader::Stop();
        ForgetHandOff();
    }
}

bool UI::Menu::OpenFor(RE::Actor* a_actor, const OSkin::API::MenuState* a_state) {
    if (!Window || !Util::IsSkinTarget(a_actor)) return false;

    g_openFor = a_actor->GetHandle();
    Window->IsOpen = true;
    logger::info("The menu opens on {} for another mod", a_actor->GetDisplayFullName());

    if (a_state) {
        PreviewPanel::StartNextAs(a_actor, *a_state);

        if (a_state->cursorX >= 0.0f && a_state->cursorY >= 0.0f) {
            g_cursorRestore = CursorRestore{.x = a_state->cursorX, .y = a_state->cursorY, .frames = 3};
        }
    }

    return true;
}

void UI::Menu::OpenInOBody(RE::Actor* a_actor) {
    // Render writes the window's state back as it ends, so the switch happens there
    if (a_actor) {
        g_switchTo = a_actor->GetHandle();
    }
}

void UI::Menu::BeginHotkeyCapture() {
    g_capturedHotkey = 0;
    g_capturingHotkey = true;
    HoldTextEntry(true);
}

void UI::Menu::CancelHotkeyCapture() {
    g_capturingHotkey = false;
    HoldTextEntry(false);
}

bool UI::Menu::IsCapturingHotkey() { return g_capturingHotkey.load(); }

std::optional<std::uint32_t> UI::Menu::TakeCapturedHotkey() {
    const auto key{g_capturedHotkey.exchange(0)};

    if (key == 0) return std::nullopt;

    // A frame after the key press, so every mod saw it while the game was still in text entry
    HoldTextEntry(false);

    return key;
}

std::string UI::Menu::GetKeyName(const std::uint32_t a_scanCode) {
    // DirectInput codes from 0x80 are the extended keys, like the arrows and the numpad's Enter
    const LONG lParam{static_cast<LONG>(((a_scanCode & 0x7F) << 16) | ((a_scanCode & 0x80) ? (1 << 24) : 0))};
    wchar_t name[64]{};

    if (GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name))) <= 0) {
        return std::format("0x{:02X}", a_scanCode);
    }

    return SKSE::stl::utf16_to_utf8(name).value_or(std::format("0x{:02X}", a_scanCode));
}
